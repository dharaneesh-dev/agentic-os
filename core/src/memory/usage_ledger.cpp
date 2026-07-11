#include "cooper/core/memory/usage_ledger.hpp"

namespace cooper::core::memory {

UsageLedger::UsageLedger(data::IDatabase& db, SessionCache& cache) : db_(db), cache_(cache) {}

void UsageLedger::RecordUsage(const data::TokenUsageEntry& entry) {
  db_.RecordTokenUsage(entry);
  cache_.RecordTokenUsage(entry);
}

}  // namespace cooper::core::memory
