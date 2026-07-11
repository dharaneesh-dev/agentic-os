#pragma once

#include "cooper/core/data/database.hpp"
#include "cooper/core/memory/session_cache.hpp"

namespace cooper::core::memory {

// the only sanctioned way to record token usage: keeps the durable IDatabase
// row and the in-memory/on-disk SessionCache from ever drifting apart.
class UsageLedger {
 public:
  UsageLedger(data::IDatabase& db, SessionCache& cache);

  void RecordUsage(const data::TokenUsageEntry& entry);

 private:
  data::IDatabase& db_;
  SessionCache& cache_;
};

}  // namespace cooper::core::memory
