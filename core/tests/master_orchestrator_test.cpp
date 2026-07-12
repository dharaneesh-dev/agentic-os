#include "cooper/core/orchestrator/master_orchestrator.hpp"

#include "cooper/core/data/sqlite_database.hpp"
#include "cooper/core/embeddings/mock_embedding_provider.hpp"
#include "cooper/core/git/repository.hpp"
#include "cooper/core/memory/session_cache.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <random>
#include <stdexcept>

using cooper::core::data::RunEvent;
using cooper::core::data::SqliteDatabase;
using cooper::core::embeddings::MockEmbeddingProvider;
using cooper::core::git::Repository;
using cooper::core::llm::ChatMessage;
using cooper::core::llm::ChatResult;
using cooper::core::llm::LlmProvider;
using cooper::core::llm::ToolCallRequest;
using cooper::core::llm::ToolDefinition;
using cooper::core::memory::SessionCache;
using cooper::core::orchestrator::MasterOrchestrator;
using cooper::core::orchestrator::MasterOrchestratorConfig;
using cooper::core::orchestrator::MasterOrchestratorResult;

namespace {

// Same shape as agent_loop_test.cpp's FakeLlmProvider: purely sequential, ignores messages/tools
// entirely, since MasterOrchestrator's role-by-role Chat() sequence is fully deterministic here.
class FakeLlmProvider : public LlmProvider {
 public:
  explicit FakeLlmProvider(std::vector<ChatResult> scripted_responses)
      : scripted_responses_(std::move(scripted_responses)) {}

  ChatResult Chat(const std::vector<ChatMessage>& /*messages*/,
                   const std::vector<ToolDefinition>& /*tools*/) override {
    if (call_index_ >= scripted_responses_.size()) {
      throw std::runtime_error("FakeLlmProvider: no more scripted responses");
    }
    return scripted_responses_[call_index_++];
  }

  bool SupportsToolCalling() const override { return true; }
  std::string Name() const override { return "fake"; }

 private:
  std::vector<ChatResult> scripted_responses_;
  size_t call_index_ = 0;
};

ChatResult ToolCall(const std::string& tool_name, const nlohmann::json& arguments, const std::string& call_id) {
  ChatResult result;
  result.has_tool_call = true;
  ToolCallRequest call;
  call.id = call_id;
  call.tool_name = tool_name;
  call.arguments_json = arguments.dump();
  result.tool_calls = {call};
  return result;
}

std::filesystem::path MakeTempDir(const std::string& tag) {
  std::random_device rd;
  std::filesystem::path dir =
      std::filesystem::temp_directory_path() / ("cooper_core_master_orch_test_" + tag + "_" + std::to_string(rd()));
  std::filesystem::create_directories(dir);
  return dir;
}

void WriteFixtureFile(const std::filesystem::path& path, const std::string& content) {
  std::ofstream file(path, std::ios::binary);
  file << content;
}

std::filesystem::path OrchestratorDir() { return std::filesystem::path(COOPER_ORCHESTRATOR_DIR); }

int CountCommits(const std::filesystem::path& repo_root) {
  std::string command = "git -C '" + repo_root.string() + "' rev-list --count HEAD 2>/dev/null";
  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return 0;
  }
  char buffer[64] = {};
  bool has_line = fgets(buffer, sizeof(buffer), pipe) != nullptr;
  pclose(pipe);
  if (!has_line) {
    return 0;
  }
  try {
    return std::stoi(buffer);
  } catch (const std::exception&) {
    return 0;
  }
}

const char* kSingleAddCheck = R"py(import sys
from solution import add

if add(2, 3) == 5:
    print("all good")
else:
    print("add wrong", file=sys.stderr)
    sys.exit(1)
)py";

const char* kAddAndSubtractCheck = R"py(import sys

ok = True
from solution import add
if add(2, 3) != 5:
    print("add wrong", file=sys.stderr)
    ok = False

try:
    from solution import subtract
except ImportError:
    subtract = None

if subtract is not None and subtract(5, 2) != 3:
    print("subtract wrong", file=sys.stderr)
    ok = False

if ok:
    print("all good")
else:
    sys.exit(1)
)py";

MasterOrchestratorConfig MakeConfig(const std::filesystem::path& repo_root, int max_retries) {
  MasterOrchestratorConfig config;
  config.repo_path = repo_root;
  config.business_requirement = "Add arithmetic helper functions to solution.py";
  config.max_retries_per_subtask = max_retries;
  config.git_author_name = "Cooper Test";
  config.git_author_email = "cooper-test@example.com";
  config.search_token_budget = 2000;
  config.run_tests_config.test_command = {"python", "check.py"};
  config.run_tests_config.docker_image = "python:3.12-slim";
  config.run_tests_config.timeout_seconds = 60;
  config.run_tests_config.orchestrator_dir = OrchestratorDir();
  return config;
}

}  // namespace

class MasterOrchestratorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    repo_root_ = MakeTempDir("repo");
    Repository::Init(repo_root_);
    db_dir_ = MakeTempDir("db");
  }

  void TearDown() override {
    std::filesystem::remove_all(repo_root_);
    std::filesystem::remove_all(db_dir_);
  }

  std::filesystem::path repo_root_;
  std::filesystem::path db_dir_;
};

// Real end-to-end: run_tests goes through the real, unmodified run_tests_cli.py against a real
// Docker container, and approved subtasks land as real libgit2 commits. No mocking of the tool
// execution or git boundary -- only the LlmProvider is faked, deterministically.
TEST_F(MasterOrchestratorTest, SingleSubtaskSucceedsFirstTry) {
  WriteFixtureFile(repo_root_ / "check.py", kSingleAddCheck);

  nlohmann::json pm_finish = {
      {"summary", "add an add function"},
      {"subtasks", nlohmann::json::array({{{"id", "add-fn"},
                                            {"description", "Add an add(a, b) function to solution.py"},
                                            {"target_files", {"solution.py"}}}})}};
  nlohmann::json scheduler_finish = {{"ordered_subtask_ids", {"add-fn"}}, {"rationale", "only one task"}};
  nlohmann::json write_args = {{"path", "solution.py"}, {"content", "def add(a, b):\n    return a + b\n"}};
  nlohmann::json coder_finish = {{"explanation", "implemented add"}};
  nlohmann::json manager_finish = {{"approved", true}, {"feedback", "looks good"}};

  std::vector<ChatResult> scripted = {
      ToolCall("finish", pm_finish, "pm-1"),
      ToolCall("finish", scheduler_finish, "sched-1"),
      ToolCall("write_file", write_args, "coder-1"),
      ToolCall("finish", coder_finish, "coder-2"),
      ToolCall("finish", manager_finish, "manager-1"),
  };
  FakeLlmProvider provider(std::move(scripted));

  SqliteDatabase db(db_dir_ / "test.db");
  SessionCache cache = SessionCache::Load(db_dir_ / "cache.json");
  MockEmbeddingProvider embedder(32);

  MasterOrchestrator orchestrator(provider, db, cache, embedder, MakeConfig(repo_root_, 3));
  MasterOrchestratorResult result = orchestrator.Run();

  EXPECT_TRUE(result.completed);
  EXPECT_FALSE(result.failed);
  ASSERT_EQ(result.subtask_outcomes.size(), 1u);
  EXPECT_EQ(result.subtask_outcomes[0].subtask_id, "add-fn");
  EXPECT_TRUE(result.subtask_outcomes[0].succeeded);
  EXPECT_EQ(result.subtask_outcomes[0].retry_count, 0);
  EXPECT_EQ(CountCommits(repo_root_), 1);
}

TEST_F(MasterOrchestratorTest, TwoDependentSubtasksBothSucceedInOrder) {
  WriteFixtureFile(repo_root_ / "check.py", kAddAndSubtractCheck);

  nlohmann::json pm_finish = {
      {"summary", "add arithmetic helpers"},
      {"subtasks",
       nlohmann::json::array(
           {{{"id", "add-fn"},
             {"description", "Add an add(a, b) function to solution.py"},
             {"target_files", {"solution.py"}}},
            {{"id", "sub-fn"},
             {"description", "Add a subtract(a, b) function to solution.py"},
             {"target_files", {"solution.py"}}}})}};
  nlohmann::json scheduler_finish = {{"ordered_subtask_ids", {"add-fn", "sub-fn"}}, {"rationale", "add before subtract"}};

  nlohmann::json write_args_1 = {{"path", "solution.py"}, {"content", "def add(a, b):\n    return a + b\n"}};
  nlohmann::json coder_finish_1 = {{"explanation", "implemented add"}};
  nlohmann::json manager_finish_1 = {{"approved", true}, {"feedback", "looks good"}};

  nlohmann::json write_args_2 = {
      {"path", "solution.py"},
      {"content", "def add(a, b):\n    return a + b\n\n\ndef subtract(a, b):\n    return a - b\n"}};
  nlohmann::json coder_finish_2 = {{"explanation", "implemented subtract"}};
  nlohmann::json manager_finish_2 = {{"approved", true}, {"feedback", "looks good"}};

  std::vector<ChatResult> scripted = {
      ToolCall("finish", pm_finish, "pm-1"),
      ToolCall("finish", scheduler_finish, "sched-1"),
      ToolCall("write_file", write_args_1, "coder-1a"),
      ToolCall("finish", coder_finish_1, "coder-1b"),
      ToolCall("finish", manager_finish_1, "manager-1"),
      ToolCall("write_file", write_args_2, "coder-2a"),
      ToolCall("finish", coder_finish_2, "coder-2b"),
      ToolCall("finish", manager_finish_2, "manager-2"),
  };
  FakeLlmProvider provider(std::move(scripted));

  SqliteDatabase db(db_dir_ / "test.db");
  SessionCache cache = SessionCache::Load(db_dir_ / "cache.json");
  MockEmbeddingProvider embedder(32);

  MasterOrchestrator orchestrator(provider, db, cache, embedder, MakeConfig(repo_root_, 3));
  MasterOrchestratorResult result = orchestrator.Run();

  EXPECT_TRUE(result.completed);
  EXPECT_FALSE(result.failed);
  ASSERT_EQ(result.subtask_outcomes.size(), 2u);
  EXPECT_EQ(result.subtask_outcomes[0].subtask_id, "add-fn");
  EXPECT_EQ(result.subtask_outcomes[1].subtask_id, "sub-fn");
  EXPECT_TRUE(result.subtask_outcomes[0].succeeded);
  EXPECT_TRUE(result.subtask_outcomes[1].succeeded);
  EXPECT_EQ(CountCommits(repo_root_), 2);
}

TEST_F(MasterOrchestratorTest, BugThenSuggestedFixSucceedsOnRetry) {
  WriteFixtureFile(repo_root_ / "check.py", kSingleAddCheck);

  nlohmann::json pm_finish = {
      {"summary", "add an add function"},
      {"subtasks", nlohmann::json::array({{{"id", "add-fn"},
                                            {"description", "Add an add(a, b) function to solution.py"},
                                            {"target_files", {"solution.py"}}}})}};
  nlohmann::json scheduler_finish = {{"ordered_subtask_ids", {"add-fn"}}, {"rationale", "only one task"}};

  nlohmann::json broken_write_args = {{"path", "solution.py"}, {"content", "def add(a, b):\n    return a - b\n"}};
  nlohmann::json broken_coder_finish = {{"explanation", "implemented add (attempt 1)"}};

  nlohmann::json diagnoser_finish = {{"diagnosis", "add() subtracts instead of adding"},
                                      {"suggested_fix", "change 'return a - b' to 'return a + b'"}};

  // Attempt 2 is scripted to actually apply the suggested fix -- this is the FakeLlmProvider
  // standing in for a real model that read the suggested_fix text fed into its prompt.
  nlohmann::json fixed_write_args = {{"path", "solution.py"}, {"content", "def add(a, b):\n    return a + b\n"}};
  nlohmann::json fixed_coder_finish = {{"explanation", "implemented add (attempt 2, applied suggested fix)"}};
  nlohmann::json manager_finish = {{"approved", true}, {"feedback", "looks good"}};

  std::vector<ChatResult> scripted = {
      ToolCall("finish", pm_finish, "pm-1"),
      ToolCall("finish", scheduler_finish, "sched-1"),
      ToolCall("write_file", broken_write_args, "coder-1a"),
      ToolCall("finish", broken_coder_finish, "coder-1b"),
      ToolCall("finish", diagnoser_finish, "diag-1"),
      ToolCall("write_file", fixed_write_args, "coder-2a"),
      ToolCall("finish", fixed_coder_finish, "coder-2b"),
      ToolCall("finish", manager_finish, "manager-1"),
  };
  FakeLlmProvider provider(std::move(scripted));

  SqliteDatabase db(db_dir_ / "test.db");
  SessionCache cache = SessionCache::Load(db_dir_ / "cache.json");
  MockEmbeddingProvider embedder(32);

  MasterOrchestrator orchestrator(provider, db, cache, embedder, MakeConfig(repo_root_, 3));
  MasterOrchestratorResult result = orchestrator.Run();

  EXPECT_TRUE(result.completed);
  EXPECT_FALSE(result.failed);
  ASSERT_EQ(result.subtask_outcomes.size(), 1u);
  EXPECT_TRUE(result.subtask_outcomes[0].succeeded);
  EXPECT_EQ(result.subtask_outcomes[0].retry_count, 1);
  // The failed first attempt must not have committed -- only the approved second attempt does.
  EXPECT_EQ(CountCommits(repo_root_), 1);
}

TEST_F(MasterOrchestratorTest, SchedulerFallsBackToOriginalOrderOnInvalidResponse) {
  WriteFixtureFile(repo_root_ / "check.py", kSingleAddCheck);

  nlohmann::json pm_finish = {
      {"summary", "add an add function"},
      {"subtasks", nlohmann::json::array({{{"id", "add-fn"},
                                            {"description", "Add an add(a, b) function to solution.py"},
                                            {"target_files", {"solution.py"}}}})}};
  // Invalid: lists the only subtask id twice instead of exactly once.
  nlohmann::json scheduler_finish = {{"ordered_subtask_ids", {"add-fn", "add-fn"}}, {"rationale", "bogus"}};
  nlohmann::json write_args = {{"path", "solution.py"}, {"content", "def add(a, b):\n    return a + b\n"}};
  nlohmann::json coder_finish = {{"explanation", "implemented add"}};
  nlohmann::json manager_finish = {{"approved", true}, {"feedback", "looks good"}};

  std::vector<ChatResult> scripted = {
      ToolCall("finish", pm_finish, "pm-1"),
      ToolCall("finish", scheduler_finish, "sched-1"),
      ToolCall("write_file", write_args, "coder-1"),
      ToolCall("finish", coder_finish, "coder-2"),
      ToolCall("finish", manager_finish, "manager-1"),
  };
  FakeLlmProvider provider(std::move(scripted));

  SqliteDatabase db(db_dir_ / "test.db");
  SessionCache cache = SessionCache::Load(db_dir_ / "cache.json");
  MockEmbeddingProvider embedder(32);

  MasterOrchestrator orchestrator(provider, db, cache, embedder, MakeConfig(repo_root_, 3));
  MasterOrchestratorResult result = orchestrator.Run();

  EXPECT_TRUE(result.completed);
  ASSERT_EQ(result.subtask_outcomes.size(), 1u);
  EXPECT_EQ(result.subtask_outcomes[0].subtask_id, "add-fn");

  std::vector<RunEvent> events = db.GetRunEvents(result.run_id);
  bool found_fallback = false;
  for (const auto& event : events) {
    if (event.event_type == "scheduler_fallback") found_fallback = true;
  }
  EXPECT_TRUE(found_fallback);
}

TEST_F(MasterOrchestratorTest, ExhaustedRetriesFailsTheWholeRun) {
  WriteFixtureFile(repo_root_ / "check.py", kSingleAddCheck);

  nlohmann::json pm_finish = {
      {"summary", "add an add function"},
      {"subtasks", nlohmann::json::array({{{"id", "add-fn"},
                                            {"description", "Add an add(a, b) function to solution.py"},
                                            {"target_files", {"solution.py"}}}})}};
  nlohmann::json scheduler_finish = {{"ordered_subtask_ids", {"add-fn"}}, {"rationale", "only one task"}};
  nlohmann::json broken_write_args = {{"path", "solution.py"}, {"content", "def add(a, b):\n    return a - b\n"}};
  nlohmann::json broken_coder_finish = {{"explanation", "implemented add"}};
  nlohmann::json diagnoser_finish = {{"diagnosis", "wrong operator"}, {"suggested_fix", "use +"}};

  std::vector<ChatResult> scripted = {
      ToolCall("finish", pm_finish, "pm-1"),
      ToolCall("finish", scheduler_finish, "sched-1"),
      ToolCall("write_file", broken_write_args, "coder-1a"),
      ToolCall("finish", broken_coder_finish, "coder-1b"),
      ToolCall("finish", diagnoser_finish, "diag-1"),
  };
  FakeLlmProvider provider(std::move(scripted));

  SqliteDatabase db(db_dir_ / "test.db");
  SessionCache cache = SessionCache::Load(db_dir_ / "cache.json");
  MockEmbeddingProvider embedder(32);

  // max_retries_per_subtask == 1: a single failing attempt exhausts the budget immediately.
  MasterOrchestrator orchestrator(provider, db, cache, embedder, MakeConfig(repo_root_, 1));
  MasterOrchestratorResult result = orchestrator.Run();

  EXPECT_FALSE(result.completed);
  EXPECT_TRUE(result.failed);
  ASSERT_EQ(result.subtask_outcomes.size(), 1u);
  EXPECT_FALSE(result.subtask_outcomes[0].succeeded);
  EXPECT_EQ(result.subtask_outcomes[0].retry_count, 1);
  EXPECT_EQ(CountCommits(repo_root_), 0);

  std::vector<RunEvent> events = db.GetRunEvents(result.run_id);
  bool found_failed = false;
  for (const auto& event : events) {
    if (event.event_type == "subtask_failed") found_failed = true;
  }
  EXPECT_TRUE(found_failed);
}
