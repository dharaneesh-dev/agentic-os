#include "cooper/core/memory/usage_ledger.hpp"

#include "cooper/core/data/sqlite_database.hpp"

#include <gtest/gtest.h>

#include <random>

using cooper::core::data::Codebase;
using cooper::core::data::SqliteDatabase;
using cooper::core::data::TokenUsageEntry;
using cooper::core::memory::SessionCache;
using cooper::core::memory::UsageLedger;

namespace {

std::filesystem::path MakeTempDir() {
  std::random_device rd;
  std::filesystem::path dir =
      std::filesystem::temp_directory_path() / ("cooper_core_usage_ledger_test_" + std::to_string(rd()));
  std::filesystem::create_directories(dir);
  return dir;
}

// Aliased locally (not `Run`): `::testing::Test` declares its own `Run()` method,
// which shadows `cooper::core::data::Run` inside any TEST_F body.
using RunModel = cooper::core::data::Run;

int64_t CreateRealRun(SqliteDatabase& db) {
  Codebase codebase;
  codebase.name = "test-codebase";
  codebase.repo_path = "/repo/test";
  codebase.created_at = "2026-07-10T00:00:00Z";
  int64_t codebase_id = db.CreateCodebase(codebase);

  RunModel run;
  run.codebase_id = codebase_id;
  run.business_requirement = "test requirement";
  run.status = "running";
  run.created_at = "2026-07-10T00:00:00Z";
  return db.CreateRun(run);
}

}  // namespace

class UsageLedgerTest : public ::testing::Test {
 protected:
  void SetUp() override { temp_dir_ = MakeTempDir(); }

  void TearDown() override { std::filesystem::remove_all(temp_dir_); }

  std::filesystem::path temp_dir_;
};

TEST_F(UsageLedgerTest, RecordUsageWritesMatchingRowToBothDbAndCache) {
  SqliteDatabase db(temp_dir_ / "usage.db");
  SessionCache cache = SessionCache::Load(temp_dir_ / "session_cache.json");
  UsageLedger ledger(db, cache);
  int64_t run_id = CreateRealRun(db);

  TokenUsageEntry entry;
  entry.run_id = run_id;
  entry.subtask_key = "subtask-1";
  entry.agent_role = "coder";
  entry.prompt_tokens = 150;
  entry.completion_tokens = 60;
  entry.estimated_cost = 0.0021;
  entry.latency_ms = 900;
  entry.created_at = "2026-07-10T00:00:00Z";

  ledger.RecordUsage(entry);

  std::vector<TokenUsageEntry> db_entries = db.GetTokenUsageForRun(run_id);
  ASSERT_EQ(db_entries.size(), 1u);
  EXPECT_EQ(db_entries[0].agent_role, "coder");
  EXPECT_EQ(db_entries[0].prompt_tokens, 150);
  EXPECT_EQ(db_entries[0].completion_tokens, 60);

  EXPECT_EQ(cache.TotalTokensForRun(), 150 + 60);
}

TEST_F(UsageLedgerTest, MultipleRecordUsageCallsStayInSync) {
  SqliteDatabase db(temp_dir_ / "usage.db");
  SessionCache cache = SessionCache::Load(temp_dir_ / "session_cache.json");
  UsageLedger ledger(db, cache);
  int64_t run_id = CreateRealRun(db);

  int64_t expected_total = 0;
  for (int i = 0; i < 10; ++i) {
    TokenUsageEntry entry;
    entry.run_id = run_id;
    entry.subtask_key = "subtask-1";
    entry.agent_role = "coder";
    entry.prompt_tokens = 20 + i;
    entry.completion_tokens = 5 + i;
    entry.estimated_cost = 0.0001 * i;
    entry.latency_ms = 100 + i;
    entry.created_at = "2026-07-10T00:00:00Z";
    ledger.RecordUsage(entry);
    expected_total += entry.prompt_tokens + entry.completion_tokens;
  }

  std::vector<TokenUsageEntry> db_entries = db.GetTokenUsageForRun(run_id);
  EXPECT_EQ(db_entries.size(), 10u);

  int64_t db_total = 0;
  for (const auto& e : db_entries) {
    db_total += e.prompt_tokens + e.completion_tokens;
  }

  EXPECT_EQ(db_total, expected_total);
  EXPECT_EQ(cache.TotalTokensForRun(), expected_total);
}
