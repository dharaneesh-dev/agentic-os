#pragma once

#include "cooper/core/agent/roles.hpp"
#include "cooper/core/data/database.hpp"
#include "cooper/core/embeddings/embedding_provider.hpp"
#include "cooper/core/llm/provider.hpp"
#include "cooper/core/memory/session_cache.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cooper::core::orchestrator {

struct MasterOrchestratorConfig {
  std::filesystem::path repo_path;
  std::string business_requirement;
  int max_retries_per_subtask;
  std::string git_author_name;
  std::string git_author_email;
  agent::RunTestsConfig run_tests_config;
  int search_token_budget;
};

struct SubtaskOutcome {
  std::string subtask_id;
  bool succeeded;
  int retry_count;
};

struct MasterOrchestratorResult {
  bool completed;
  bool failed;
  std::vector<SubtaskOutcome> subtask_outcomes;
  int64_t run_id;
};

// Drives Product Manager -> Scheduler -> (Coder -> tests -> Manager | Diagnoser) per subtask,
// persisting Run/Subtask/RunEvent rows and token usage as it goes. See IMPLEMENTATION_PLAN.md
// Phase 6 for the full step-by-step contract this implements.
class MasterOrchestrator {
 public:
  MasterOrchestrator(llm::LlmProvider& provider, data::IDatabase& db, memory::SessionCache& cache,
                      embeddings::EmbeddingProvider& embedder, MasterOrchestratorConfig config);

  MasterOrchestratorResult Run();

 private:
  void AppendEvent(int64_t run_id, const std::string& event_type, const std::string& data_json);

  llm::LlmProvider& provider_;
  data::IDatabase& db_;
  memory::SessionCache& cache_;
  embeddings::EmbeddingProvider& embedder_;
  MasterOrchestratorConfig config_;
};

}  // namespace cooper::core::orchestrator
