#include "cooper/core/memory/session_cache.hpp"

#include <fstream>

namespace cooper::core::memory {

namespace {

nlohmann::json ToJson(const data::TokenUsageEntry& entry) {
  return nlohmann::json{
      {"id", entry.id},
      {"run_id", entry.run_id},
      {"subtask_key", entry.subtask_key},
      {"agent_role", entry.agent_role},
      {"prompt_tokens", entry.prompt_tokens},
      {"completion_tokens", entry.completion_tokens},
      {"estimated_cost", entry.estimated_cost},
      {"latency_ms", entry.latency_ms},
      {"created_at", entry.created_at},
  };
}

nlohmann::json EmptyState() {
  nlohmann::json state = nlohmann::json::object();
  state["chunk_cache"] = nlohmann::json::object();
  state["attempts"] = nlohmann::json::object();
  state["token_usage"] = nlohmann::json::array();
  return state;
}

}  // namespace

SessionCache::SessionCache(std::filesystem::path path, nlohmann::json state)
    : path_(std::move(path)), state_(std::move(state)) {}

SessionCache SessionCache::Load(const std::filesystem::path& path) {
  if (std::filesystem::exists(path)) {
    std::ifstream file(path);
    nlohmann::json state;
    file >> state;
    return SessionCache(path, std::move(state));
  }
  return SessionCache(path, EmptyState());
}

void SessionCache::Save() const {
  std::ofstream file(path_);
  file << state_.dump(2);
}

void SessionCache::SetChunkCache(const std::string& subtask_key, const std::string& file_path,
                                  const std::string& content) {
  state_["chunk_cache"][subtask_key][file_path] = content;
  Save();
}

std::optional<std::string> SessionCache::GetChunkCache(const std::string& subtask_key,
                                                         const std::string& file_path) const {
  const auto& chunk_cache = state_.at("chunk_cache");
  auto subtask_it = chunk_cache.find(subtask_key);
  if (subtask_it == chunk_cache.end()) {
    return std::nullopt;
  }
  auto file_it = subtask_it->find(file_path);
  if (file_it == subtask_it->end()) {
    return std::nullopt;
  }
  return file_it->get<std::string>();
}

void SessionCache::AppendAttempt(const std::string& subtask_key, const nlohmann::json& attempt) {
  state_["attempts"][subtask_key].push_back(attempt);
  Save();
}

std::vector<nlohmann::json> SessionCache::GetAttempts(const std::string& subtask_key) const {
  const auto& attempts = state_.at("attempts");
  auto it = attempts.find(subtask_key);
  if (it == attempts.end()) {
    return {};
  }
  return std::vector<nlohmann::json>(it->begin(), it->end());
}

void SessionCache::RecordTokenUsage(const data::TokenUsageEntry& entry) {
  state_["token_usage"].push_back(ToJson(entry));
  Save();
}

int64_t SessionCache::TotalTokensForRun() const {
  int64_t total = 0;
  for (const auto& item : state_.at("token_usage")) {
    total += item.at("prompt_tokens").get<int64_t>() + item.at("completion_tokens").get<int64_t>();
  }
  return total;
}

}  // namespace cooper::core::memory
