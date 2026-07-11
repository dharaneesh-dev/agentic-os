#pragma once

#include "cooper/core/data/database.hpp"

#include <filesystem>
#include <memory>

typedef struct sqlite3 sqlite3;

namespace cooper::core::data {

class SqliteDatabase : public IDatabase {
 public:
  explicit SqliteDatabase(const std::filesystem::path& path);

  int64_t CreateCodebase(const Codebase& codebase) override;
  std::optional<Codebase> GetCodebase(int64_t id) override;

  int64_t CreateRun(const Run& run) override;
  std::optional<Run> GetRun(int64_t id) override;
  void UpdateRunStatus(int64_t run_id, const std::string& status) override;

  int64_t AppendRunEvent(const RunEvent& event) override;
  std::vector<RunEvent> GetRunEvents(int64_t run_id) override;

  int64_t CreateSubtask(const Subtask& subtask) override;
  std::vector<Subtask> GetSubtasksForRun(int64_t run_id) override;
  void UpdateSubtaskStatus(int64_t subtask_id, const std::string& status) override;

  int64_t CreateSubtaskAttempt(const SubtaskAttempt& attempt) override;
  std::vector<SubtaskAttempt> GetAttemptsForSubtask(int64_t subtask_id) override;

  int64_t CreateKnowledgeDocument(const KnowledgeDocument& document) override;
  int64_t CreateKnowledgeChunk(const KnowledgeChunk& chunk) override;
  std::vector<KnowledgeChunk> GetChunksForDocument(int64_t document_id) override;

  int64_t UpsertProviderCredential(const ProviderCredential& credential) override;
  std::optional<ProviderCredential> GetProviderCredential(const std::string& user_id,
                                                           const std::string& provider) override;

  int64_t RecordTokenUsage(const TokenUsageEntry& entry) override;
  std::vector<TokenUsageEntry> GetTokenUsageForRun(int64_t run_id) override;

 private:
  void ExecOrThrow(const char* sql, const std::string& context);
  void InitializeSchema();

  struct Sqlite3Deleter {
    void operator()(sqlite3* handle) const;
  };

  std::unique_ptr<sqlite3, Sqlite3Deleter> db_;
};

}  // namespace cooper::core::data
