#include "cooper/core/memory/session_cache.hpp"

#include <gtest/gtest.h>

#include <random>

using cooper::core::data::TokenUsageEntry;
using cooper::core::memory::SessionCache;

namespace {

std::filesystem::path MakeTempCachePath() {
  std::random_device rd;
  std::filesystem::path dir =
      std::filesystem::temp_directory_path() / ("cooper_core_session_cache_test_" + std::to_string(rd()));
  std::filesystem::create_directories(dir);
  return dir / "session_cache.json";
}

}  // namespace

class SessionCacheTest : public ::testing::Test {
 protected:
  void SetUp() override { cache_path_ = MakeTempCachePath(); }

  void TearDown() override { std::filesystem::remove_all(cache_path_.parent_path()); }

  std::filesystem::path cache_path_;
};

TEST_F(SessionCacheTest, LoadOnMissingPathStartsEmpty) {
  SessionCache cache = SessionCache::Load(cache_path_);
  EXPECT_FALSE(cache.GetChunkCache("subtask-1", "src/foo.cpp").has_value());
  EXPECT_TRUE(cache.GetAttempts("subtask-1").empty());
  EXPECT_EQ(cache.TotalTokensForRun(), 0);
}

TEST_F(SessionCacheTest, ChunkCacheSetAndGetHitAndMiss) {
  SessionCache cache = SessionCache::Load(cache_path_);
  cache.SetChunkCache("subtask-1", "src/foo.cpp", "int foo() { return 42; }");

  auto hit = cache.GetChunkCache("subtask-1", "src/foo.cpp");
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(*hit, "int foo() { return 42; }");

  EXPECT_FALSE(cache.GetChunkCache("subtask-1", "src/never_set.cpp").has_value());
  EXPECT_FALSE(cache.GetChunkCache("subtask-never-set", "src/foo.cpp").has_value());
}

TEST_F(SessionCacheTest, AttemptHistoryAppendsAndReadsBackInOrder) {
  SessionCache cache = SessionCache::Load(cache_path_);
  cache.AppendAttempt("subtask-1", nlohmann::json{{"attempt", 1}, {"result", "failed"}});
  cache.AppendAttempt("subtask-1", nlohmann::json{{"attempt", 2}, {"result", "succeeded"}});

  std::vector<nlohmann::json> attempts = cache.GetAttempts("subtask-1");
  ASSERT_EQ(attempts.size(), 2u);
  EXPECT_EQ(attempts[0]["attempt"], 1);
  EXPECT_EQ(attempts[0]["result"], "failed");
  EXPECT_EQ(attempts[1]["attempt"], 2);
  EXPECT_EQ(attempts[1]["result"], "succeeded");
}

TEST_F(SessionCacheTest, CrashRecoveryRoundTrip) {
  {
    SessionCache first = SessionCache::Load(cache_path_);
    first.SetChunkCache("subtask-1", "src/foo.cpp", "content-foo");
    first.AppendAttempt("subtask-1", nlohmann::json{{"attempt", 1}, {"result", "failed"}});

    TokenUsageEntry entry;
    entry.run_id = 7;
    entry.subtask_key = "subtask-1";
    entry.agent_role = "coder";
    entry.prompt_tokens = 100;
    entry.completion_tokens = 25;
    entry.estimated_cost = 0.001;
    entry.latency_ms = 500;
    entry.created_at = "2026-07-10T00:00:00Z";
    first.RecordTokenUsage(entry);
    // first goes out of scope here, simulating a process restart with no
    // further explicit action — Save() already happened write-through.
  }

  SessionCache second = SessionCache::Load(cache_path_);
  auto chunk = second.GetChunkCache("subtask-1", "src/foo.cpp");
  ASSERT_TRUE(chunk.has_value());
  EXPECT_EQ(*chunk, "content-foo");

  auto attempts = second.GetAttempts("subtask-1");
  ASSERT_EQ(attempts.size(), 1u);
  EXPECT_EQ(attempts[0]["result"], "failed");

  EXPECT_EQ(second.TotalTokensForRun(), 125);
}

TEST_F(SessionCacheTest, HighVolumeTokenUsageStaysCorrectAndReloads) {
  constexpr int kEntryCount = 500;
  int64_t expected_total = 0;

  {
    SessionCache cache = SessionCache::Load(cache_path_);
    for (int i = 0; i < kEntryCount; ++i) {
      TokenUsageEntry entry;
      entry.run_id = 1;
      entry.subtask_key = "subtask-" + std::to_string(i % 10);
      entry.agent_role = "coder";
      entry.prompt_tokens = 10 + (i % 7);
      entry.completion_tokens = 5 + (i % 3);
      entry.estimated_cost = 0.0001 * i;
      entry.latency_ms = 100 + i;
      entry.created_at = "2026-07-10T00:00:00Z";
      cache.RecordTokenUsage(entry);
      expected_total += entry.prompt_tokens + entry.completion_tokens;
    }
    EXPECT_EQ(cache.TotalTokensForRun(), expected_total);
  }

  SessionCache reloaded = SessionCache::Load(cache_path_);
  EXPECT_EQ(reloaded.TotalTokensForRun(), expected_total);
}
