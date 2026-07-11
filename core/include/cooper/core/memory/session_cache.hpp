#pragma once

#include "cooper/core/data/models.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cooper::core::memory {

class SessionCache {
 public:
  static SessionCache Load(const std::filesystem::path& path);

  void Save() const;

  void SetChunkCache(const std::string& subtask_key, const std::string& file_path, const std::string& content);
  std::optional<std::string> GetChunkCache(const std::string& subtask_key, const std::string& file_path) const;

  void AppendAttempt(const std::string& subtask_key, const nlohmann::json& attempt);
  std::vector<nlohmann::json> GetAttempts(const std::string& subtask_key) const;

  void RecordTokenUsage(const data::TokenUsageEntry& entry);
  int64_t TotalTokensForRun() const;

 private:
  explicit SessionCache(std::filesystem::path path, nlohmann::json state);

  std::filesystem::path path_;
  nlohmann::json state_;
};

}  // namespace cooper::core::memory
