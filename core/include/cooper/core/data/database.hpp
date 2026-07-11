#pragma once

#include "cooper/core/data/models.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cooper::core::data {

class IDatabase {
 public:
  virtual ~IDatabase() = default;

  virtual int64_t CreateCodebase(const Codebase& codebase) = 0;
  virtual std::optional<Codebase> GetCodebase(int64_t id) = 0;

  virtual int64_t CreateRun(const Run& run) = 0;
  virtual std::optional<Run> GetRun(int64_t id) = 0;
  virtual void UpdateRunStatus(int64_t run_id, const std::string& status) = 0;

  // append-only: no update/delete method is exposed for run events by design.
  virtual int64_t AppendRunEvent(const RunEvent& event) = 0;
  virtual std::vector<RunEvent> GetRunEvents(int64_t run_id) = 0;

  virtual int64_t CreateSubtask(const Subtask& subtask) = 0;
  virtual std::vector<Subtask> GetSubtasksForRun(int64_t run_id) = 0;
  virtual void UpdateSubtaskStatus(int64_t subtask_id, const std::string& status) = 0;

  virtual int64_t CreateSubtaskAttempt(const SubtaskAttempt& attempt) = 0;
  virtual std::vector<SubtaskAttempt> GetAttemptsForSubtask(int64_t subtask_id) = 0;

  virtual int64_t CreateKnowledgeDocument(const KnowledgeDocument& document) = 0;
  virtual int64_t CreateKnowledgeChunk(const KnowledgeChunk& chunk) = 0;
  virtual std::vector<KnowledgeChunk> GetChunksForDocument(int64_t document_id) = 0;

  // upsert keyed on (user_id, provider): a second call for the same pair updates in place.
  virtual int64_t UpsertProviderCredential(const ProviderCredential& credential) = 0;
  virtual std::optional<ProviderCredential> GetProviderCredential(const std::string& user_id,
                                                                   const std::string& provider) = 0;

  virtual int64_t RecordTokenUsage(const TokenUsageEntry& entry) = 0;
  virtual std::vector<TokenUsageEntry> GetTokenUsageForRun(int64_t run_id) = 0;
};

}  // namespace cooper::core::data
