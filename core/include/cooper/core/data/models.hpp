#pragma once

#include <cstdint>
#include <string>

namespace cooper::core::data {

struct Codebase {
  int64_t id = 0;
  std::string name;
  std::string repo_path;
  std::string created_at;
};

struct Run {
  int64_t id = 0;
  int64_t codebase_id = 0;
  std::string business_requirement;
  std::string status;
  std::string created_at;
};

struct RunEvent {
  int64_t id = 0;
  int64_t run_id = 0;
  std::string event_type;
  std::string data_json;
  std::string created_at;
};

struct Subtask {
  int64_t id = 0;
  int64_t run_id = 0;
  std::string subtask_key;
  std::string description;
  std::string status;
};

struct SubtaskAttempt {
  int64_t id = 0;
  int64_t subtask_id = 0;
  int attempt_number = 0;
  std::string status;
  std::string created_at;
};

struct KnowledgeDocument {
  int64_t id = 0;
  int64_t codebase_id = 0;
  std::string source;
  std::string title;
};

struct KnowledgeChunk {
  int64_t id = 0;
  int64_t document_id = 0;
  std::string content;
  std::string embedding_json;
};

struct ProviderCredential {
  int64_t id = 0;
  std::string user_id;
  std::string provider;
  std::string credential_value;
};

struct TokenUsageEntry {
  int64_t id = 0;
  int64_t run_id = 0;
  std::string subtask_key;
  std::string agent_role;
  int prompt_tokens = 0;
  int completion_tokens = 0;
  double estimated_cost = 0.0;
  int64_t latency_ms = 0;
  std::string created_at;
};

}  // namespace cooper::core::data
