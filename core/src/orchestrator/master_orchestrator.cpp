#include "cooper/core/orchestrator/master_orchestrator.hpp"

#include "cooper/core/agent/agent_loop.hpp"
#include "cooper/core/agent/run_tests_tool.hpp"
#include "cooper/core/git/repository.hpp"
#include "cooper/core/memory/usage_ledger.hpp"
#include "cooper/core/orchestrator/tracking_provider.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace cooper::core::orchestrator {

namespace {

std::string NowIso8601() {
  std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm utc_tm{};
#ifdef _WIN32
  gmtime_s(&utc_tm, &now);
#else
  gmtime_r(&now, &utc_tm);
#endif
  std::ostringstream stream;
  stream << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
  return stream.str();
}

std::string RepoBasename(const std::filesystem::path& path) {
  std::string name = path.filename().string();
  return name.empty() ? path.parent_path().filename().string() : name;
}

std::string ExtractStringField(const std::string& json_text, const std::string& field, const std::string& fallback) {
  try {
    return nlohmann::json::parse(json_text).value(field, fallback);
  } catch (const nlohmann::json::exception&) {
    return fallback;
  }
}

struct SubtaskInfo {
  std::string id;
  std::string description;
  std::vector<std::string> target_files;
  int64_t db_id = 0;
};

std::string FormatTargetFiles(const std::vector<std::string>& target_files) {
  std::ostringstream stream;
  stream << "[";
  for (size_t i = 0; i < target_files.size(); ++i) {
    if (i > 0) stream << ", ";
    stream << target_files[i];
  }
  stream << "]";
  return stream.str();
}

SubtaskInfo* FindSubtask(std::vector<SubtaskInfo>& subtasks, const std::string& id) {
  for (auto& subtask : subtasks) {
    if (subtask.id == id) return &subtask;
  }
  return nullptr;
}

}  // namespace

MasterOrchestrator::MasterOrchestrator(llm::LlmProvider& provider, data::IDatabase& db, memory::SessionCache& cache,
                                        embeddings::EmbeddingProvider& embedder, MasterOrchestratorConfig config)
    : provider_(provider), db_(db), cache_(cache), embedder_(embedder), config_(std::move(config)) {}

void MasterOrchestrator::AppendEvent(int64_t run_id, const std::string& event_type, const std::string& data_json) {
  data::RunEvent event;
  event.run_id = run_id;
  event.event_type = event_type;
  event.data_json = data_json;
  event.created_at = NowIso8601();
  db_.AppendRunEvent(event);
}

MasterOrchestratorResult MasterOrchestrator::Run() {
  MasterOrchestratorResult final_result;
  final_result.completed = false;
  final_result.failed = false;

  data::Codebase codebase;
  codebase.name = RepoBasename(config_.repo_path);
  codebase.repo_path = config_.repo_path.string();
  codebase.created_at = NowIso8601();
  int64_t codebase_id = db_.CreateCodebase(codebase);

  data::Run run;
  run.codebase_id = codebase_id;
  run.business_requirement = config_.business_requirement;
  run.status = "running";
  run.created_at = NowIso8601();
  int64_t run_id = db_.CreateRun(run);
  final_result.run_id = run_id;
  AppendEvent(run_id, "run_started", "{}");

  memory::UsageLedger ledger(db_, cache_);
  TrackingProvider tracking_provider(provider_, ledger, run_id);

  // Step 2: Product Manager.
  tracking_provider.SetContext("", "product_manager");
  agent::RoleSetup pm_setup = agent::MakeProductManagerRole(config_.repo_path, embedder_, config_.search_token_budget);
  agent::AgentLoop pm_loop(tracking_provider, std::move(pm_setup.tools), pm_setup.config);
  agent::AgentLoopResult pm_result = pm_loop.Run(config_.business_requirement);

  if (!pm_result.finished) {
    AppendEvent(run_id, "run_failed", nlohmann::json{{"reason", "product_manager_did_not_finish"}}.dump());
    db_.UpdateRunStatus(run_id, "failed");
    final_result.failed = true;
    return final_result;
  }

  nlohmann::json pm_json = nlohmann::json::parse(pm_result.finish_arguments_json);
  std::vector<SubtaskInfo> subtasks;
  for (const auto& item : pm_json.at("subtasks")) {
    SubtaskInfo info;
    info.id = item.at("id").get<std::string>();
    info.description = item.at("description").get<std::string>();
    info.target_files = item.at("target_files").get<std::vector<std::string>>();

    data::Subtask row;
    row.run_id = run_id;
    row.subtask_key = info.id;
    row.description = info.description;
    row.status = "pending";
    info.db_id = db_.CreateSubtask(row);

    subtasks.push_back(std::move(info));
  }
  AppendEvent(run_id, "product_manager_completed",
              nlohmann::json{{"summary", pm_json.value("summary", "")}}.dump());

  // Step 3: Scheduler.
  std::vector<std::string> original_order;
  std::ostringstream scheduler_prompt;
  scheduler_prompt << "Subtasks:\n";
  for (const auto& subtask : subtasks) {
    original_order.push_back(subtask.id);
    scheduler_prompt << "- id=" << subtask.id << ", description=" << subtask.description
                      << ", target_files=" << FormatTargetFiles(subtask.target_files) << "\n";
  }

  tracking_provider.SetContext("", "scheduler");
  agent::RoleSetup scheduler_setup = agent::MakeSchedulerRole();
  agent::AgentLoop scheduler_loop(tracking_provider, std::move(scheduler_setup.tools), scheduler_setup.config);
  agent::AgentLoopResult scheduler_result = scheduler_loop.Run(scheduler_prompt.str());

  std::vector<std::string> ordered_ids = original_order;
  if (scheduler_result.finished) {
    try {
      nlohmann::json scheduler_json = nlohmann::json::parse(scheduler_result.finish_arguments_json);
      std::vector<std::string> candidate = scheduler_json.at("ordered_subtask_ids").get<std::vector<std::string>>();

      std::vector<std::string> sorted_candidate = candidate;
      std::vector<std::string> sorted_original = original_order;
      std::sort(sorted_candidate.begin(), sorted_candidate.end());
      std::sort(sorted_original.begin(), sorted_original.end());

      if (sorted_candidate == sorted_original) {
        ordered_ids = candidate;
        AppendEvent(run_id, "scheduler_completed",
                    nlohmann::json{{"ordered_subtask_ids", ordered_ids},
                                   {"rationale", scheduler_json.value("rationale", "")}}
                        .dump());
      } else {
        AppendEvent(run_id, "scheduler_fallback",
                    nlohmann::json{{"reason", "invalid ordering returned"},
                                   {"returned", candidate},
                                   {"expected", original_order}}
                        .dump());
      }
    } catch (const nlohmann::json::exception& error) {
      AppendEvent(run_id, "scheduler_fallback",
                  nlohmann::json{{"reason", std::string("failed to parse scheduler output: ") + error.what()}}
                      .dump());
    }
  } else {
    AppendEvent(run_id, "scheduler_fallback", nlohmann::json{{"reason", "scheduler_did_not_finish"}}.dump());
  }

  // Step 4: run each subtask in the resolved order.
  for (const std::string& subtask_id : ordered_ids) {
    SubtaskInfo* info = FindSubtask(subtasks, subtask_id);
    if (info == nullptr) {
      throw std::runtime_error("MasterOrchestrator: resolved subtask id '" + subtask_id + "' not found");
    }

    int retry_count = 0;
    std::string suggested_fix_text;
    bool subtask_succeeded = false;

    while (retry_count < config_.max_retries_per_subtask) {
      tracking_provider.SetContext(subtask_id, "coder");

      std::ostringstream coder_prompt;
      if (!suggested_fix_text.empty()) {
        coder_prompt << "The previous attempt at this subtask failed. Apply this suggested fix:\n"
                      << suggested_fix_text << "\n\n";
      }
      coder_prompt << "Business requirement:\n"
                   << config_.business_requirement << "\n\nSubtask: " << info->description
                   << "\n\nTarget files: " << FormatTargetFiles(info->target_files);

      agent::RoleSetup coder_setup =
          agent::MakeCoderRole(config_.repo_path, embedder_, config_.search_token_budget, config_.run_tests_config);
      agent::AgentLoop coder_loop(tracking_provider, std::move(coder_setup.tools), coder_setup.config);
      agent::AgentLoopResult coder_result = coder_loop.Run(coder_prompt.str());

      if (!coder_result.finished) {
        AppendEvent(run_id, "subtask_failed",
                    nlohmann::json{{"subtask_id", subtask_id}, {"reason", "coder_did_not_finish"}}.dump());
        db_.UpdateSubtaskStatus(info->db_id, "failed");
        db_.UpdateRunStatus(run_id, "failed");
        final_result.failed = true;
        final_result.subtask_outcomes.push_back({subtask_id, false, retry_count});
        return final_result;
      }

      // Authoritative gate: re-run the exact same test mechanism the Coder's own run_tests tool
      // uses, independently of whatever the Coder's own finish/tool calls claimed.
      agent::RunTestsTool run_tests_tool(config_.repo_path, config_.run_tests_config.test_command,
                                          config_.run_tests_config.docker_image,
                                          config_.run_tests_config.timeout_seconds,
                                          config_.run_tests_config.orchestrator_dir,
                                          config_.run_tests_config.python_executable);
      agent::RunTestsResult test_result = run_tests_tool.RunOnce();

      data::SubtaskAttempt attempt_row;
      attempt_row.subtask_id = info->db_id;
      attempt_row.attempt_number = retry_count + 1;
      attempt_row.status = test_result.passed ? "passed" : "failed";
      attempt_row.created_at = NowIso8601();
      db_.CreateSubtaskAttempt(attempt_row);
      AppendEvent(run_id, "subtask_attempt",
                  nlohmann::json{{"subtask_id", subtask_id},
                                 {"attempt_number", retry_count + 1},
                                 {"status", attempt_row.status}}
                      .dump());

      if (test_result.passed) {
        tracking_provider.SetContext(subtask_id, "manager");

        std::ostringstream manager_prompt;
        manager_prompt << "Subtask: " << info->description << "\n\nCoder's explanation: "
                       << ExtractStringField(coder_result.finish_arguments_json, "explanation", "")
                       << "\n\nThe test suite passed for this change.";

        agent::RoleSetup manager_setup = agent::MakeManagerRole(config_.repo_path);
        agent::AgentLoop manager_loop(tracking_provider, std::move(manager_setup.tools), manager_setup.config);
        agent::AgentLoopResult manager_result = manager_loop.Run(manager_prompt.str());

        bool approved = false;
        std::string feedback;
        if (manager_result.finished) {
          try {
            nlohmann::json manager_json = nlohmann::json::parse(manager_result.finish_arguments_json);
            approved = manager_json.value("approved", false);
            feedback = manager_json.value("feedback", "");
          } catch (const nlohmann::json::exception&) {
            feedback = "manager: failed to parse finish arguments";
          }
        } else {
          feedback = "manager did not finish";
        }

        if (approved) {
          git::Repository repo = git::Repository::Open(config_.repo_path);
          repo.StageAll();
          repo.Commit(info->description, config_.git_author_name, config_.git_author_email);

          db_.UpdateSubtaskStatus(info->db_id, "completed");
          AppendEvent(run_id, "subtask_completed", nlohmann::json{{"subtask_id", subtask_id}}.dump());
          subtask_succeeded = true;
          break;
        }

        suggested_fix_text = feedback;
        ++retry_count;
      } else {
        tracking_provider.SetContext(subtask_id, "diagnoser");

        std::ostringstream diagnoser_prompt;
        diagnoser_prompt << "Subtask: " << info->description
                         << "\n\nTarget files: " << FormatTargetFiles(info->target_files)
                         << "\n\nThe test suite failed.\n\nstdout:\n"
                         << test_result.test_stdout << "\n\nstderr:\n" << test_result.test_stderr;

        agent::RoleSetup diagnoser_setup =
            agent::MakeDiagnoserRole(config_.repo_path, embedder_, config_.search_token_budget);
        agent::AgentLoop diagnoser_loop(tracking_provider, std::move(diagnoser_setup.tools), diagnoser_setup.config);
        agent::AgentLoopResult diagnoser_result = diagnoser_loop.Run(diagnoser_prompt.str());

        if (diagnoser_result.finished) {
          std::string diagnosis = ExtractStringField(diagnoser_result.finish_arguments_json, "diagnosis", "");
          std::string fix = ExtractStringField(diagnoser_result.finish_arguments_json, "suggested_fix", "");
          suggested_fix_text = "Diagnosis: " + diagnosis + "\nSuggested fix: " + fix;
        }
        ++retry_count;
      }
    }

    if (!subtask_succeeded) {
      AppendEvent(run_id, "subtask_failed",
                  nlohmann::json{{"subtask_id", subtask_id}, {"retry_count", retry_count}}.dump());
      db_.UpdateSubtaskStatus(info->db_id, "failed");
      db_.UpdateRunStatus(run_id, "failed");
      final_result.failed = true;
      final_result.subtask_outcomes.push_back({subtask_id, false, retry_count});
      return final_result;
    }

    final_result.subtask_outcomes.push_back({subtask_id, true, retry_count});
  }

  AppendEvent(run_id, "run_completed", "{}");
  db_.UpdateRunStatus(run_id, "completed");
  final_result.completed = true;
  return final_result;
}

}  // namespace cooper::core::orchestrator
