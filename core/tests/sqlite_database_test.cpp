#include "cooper/core/data/sqlite_database.hpp"

#include <gtest/gtest.h>

#include <random>

using cooper::core::data::Codebase;
using cooper::core::data::KnowledgeChunk;
using cooper::core::data::KnowledgeDocument;
using cooper::core::data::ProviderCredential;
using cooper::core::data::RunEvent;
using cooper::core::data::SqliteDatabase;
using cooper::core::data::Subtask;
using cooper::core::data::SubtaskAttempt;
using cooper::core::data::TokenUsageEntry;

// `Run` is not brought in via `using` because ::testing::Test declares a
// Run() method that shadows the type name inside TEST_F fixture bodies;
// it is referenced as cooper::core::data::Run at each use site instead.
using RunModel = cooper::core::data::Run;

namespace {

std::filesystem::path MakeTempDbPath() {
  std::random_device rd;
  std::filesystem::path dir =
      std::filesystem::temp_directory_path() / ("cooper_core_sqlite_test_" + std::to_string(rd()));
  std::filesystem::create_directories(dir);
  return dir / "test.db";
}

}  // namespace

class SqliteDatabaseTest : public ::testing::Test {
 protected:
  void SetUp() override { db_path_ = MakeTempDbPath(); }

  void TearDown() override { std::filesystem::remove_all(db_path_.parent_path()); }

  std::filesystem::path db_path_;
};

TEST_F(SqliteDatabaseTest, CreateAndGetCodebase) {
  SqliteDatabase db(db_path_);

  Codebase codebase;
  codebase.name = "cooper-core";
  codebase.repo_path = "/repo/cooper";
  codebase.created_at = "2026-07-10T00:00:00Z";

  int64_t id = db.CreateCodebase(codebase);
  EXPECT_GT(id, 0);

  auto fetched = db.GetCodebase(id);
  ASSERT_TRUE(fetched.has_value());
  EXPECT_EQ(fetched->id, id);
  EXPECT_EQ(fetched->name, "cooper-core");
  EXPECT_EQ(fetched->repo_path, "/repo/cooper");
  EXPECT_EQ(fetched->created_at, "2026-07-10T00:00:00Z");
}

TEST_F(SqliteDatabaseTest, GetCodebaseMissingReturnsNullopt) {
  SqliteDatabase db(db_path_);
  EXPECT_FALSE(db.GetCodebase(999).has_value());
}

TEST_F(SqliteDatabaseTest, CreateAndGetRunAndUpdateStatus) {
  SqliteDatabase db(db_path_);

  Codebase codebase;
  codebase.name = "cooper-core";
  codebase.repo_path = "/repo/cooper";
  codebase.created_at = "2026-07-10T00:00:00Z";
  int64_t codebase_id = db.CreateCodebase(codebase);

  RunModel run;
  run.codebase_id = codebase_id;
  run.business_requirement = "Add feature X";
  run.status = "pending";
  run.created_at = "2026-07-10T00:01:00Z";
  int64_t run_id = db.CreateRun(run);
  EXPECT_GT(run_id, 0);

  auto fetched = db.GetRun(run_id);
  ASSERT_TRUE(fetched.has_value());
  EXPECT_EQ(fetched->codebase_id, codebase_id);
  EXPECT_EQ(fetched->status, "pending");

  db.UpdateRunStatus(run_id, "completed");
  auto updated = db.GetRun(run_id);
  ASSERT_TRUE(updated.has_value());
  EXPECT_EQ(updated->status, "completed");
}

TEST_F(SqliteDatabaseTest, RunEventsAreAppendOnlyAndOrderedByInsertion) {
  SqliteDatabase db(db_path_);

  Codebase codebase;
  codebase.created_at = "2026-07-10T00:00:00Z";
  int64_t codebase_id = db.CreateCodebase(codebase);

  RunModel run;
  run.codebase_id = codebase_id;
  run.status = "running";
  run.created_at = "2026-07-10T00:01:00Z";
  int64_t run_id = db.CreateRun(run);

  for (int i = 0; i < 5; ++i) {
    RunEvent event;
    event.run_id = run_id;
    event.event_type = "step";
    event.data_json = "{\"step\":" + std::to_string(i) + "}";
    event.created_at = "2026-07-10T00:0" + std::to_string(i) + ":00Z";
    db.AppendRunEvent(event);
  }

  std::vector<RunEvent> events = db.GetRunEvents(run_id);
  ASSERT_EQ(events.size(), 5u);
  for (size_t i = 0; i < events.size(); ++i) {
    EXPECT_EQ(events[i].data_json, "{\"step\":" + std::to_string(i) + "}");
    if (i > 0) {
      EXPECT_GT(events[i].id, events[i - 1].id);
    }
  }

  // Append-only is enforced by IDatabase's interface shape: no
  // UpdateRunEvent/DeleteRunEvent method exists to call here.
}

TEST_F(SqliteDatabaseTest, SubtaskAndAttemptLifecycle) {
  SqliteDatabase db(db_path_);

  Codebase codebase;
  codebase.created_at = "2026-07-10T00:00:00Z";
  int64_t codebase_id = db.CreateCodebase(codebase);

  RunModel run;
  run.codebase_id = codebase_id;
  run.status = "running";
  run.created_at = "2026-07-10T00:01:00Z";
  int64_t run_id = db.CreateRun(run);

  Subtask subtask;
  subtask.run_id = run_id;
  subtask.subtask_key = "subtask-1";
  subtask.description = "Implement foo";
  subtask.status = "pending";
  int64_t subtask_id = db.CreateSubtask(subtask);
  EXPECT_GT(subtask_id, 0);

  std::vector<Subtask> subtasks = db.GetSubtasksForRun(run_id);
  ASSERT_EQ(subtasks.size(), 1u);
  EXPECT_EQ(subtasks[0].subtask_key, "subtask-1");

  db.UpdateSubtaskStatus(subtask_id, "in_progress");
  subtasks = db.GetSubtasksForRun(run_id);
  EXPECT_EQ(subtasks[0].status, "in_progress");

  SubtaskAttempt attempt1;
  attempt1.subtask_id = subtask_id;
  attempt1.attempt_number = 1;
  attempt1.status = "failed";
  attempt1.created_at = "2026-07-10T00:02:00Z";
  db.CreateSubtaskAttempt(attempt1);

  SubtaskAttempt attempt2;
  attempt2.subtask_id = subtask_id;
  attempt2.attempt_number = 2;
  attempt2.status = "succeeded";
  attempt2.created_at = "2026-07-10T00:03:00Z";
  db.CreateSubtaskAttempt(attempt2);

  std::vector<SubtaskAttempt> attempts = db.GetAttemptsForSubtask(subtask_id);
  ASSERT_EQ(attempts.size(), 2u);
  EXPECT_EQ(attempts[0].attempt_number, 1);
  EXPECT_EQ(attempts[0].status, "failed");
  EXPECT_EQ(attempts[1].attempt_number, 2);
  EXPECT_EQ(attempts[1].status, "succeeded");
}

TEST_F(SqliteDatabaseTest, KnowledgeDocumentAndChunkLifecycle) {
  SqliteDatabase db(db_path_);

  Codebase codebase;
  codebase.created_at = "2026-07-10T00:00:00Z";
  int64_t codebase_id = db.CreateCodebase(codebase);

  KnowledgeDocument document;
  document.codebase_id = codebase_id;
  document.source = "repo";
  document.title = "README";
  int64_t document_id = db.CreateKnowledgeDocument(document);
  EXPECT_GT(document_id, 0);

  KnowledgeChunk chunk1;
  chunk1.document_id = document_id;
  chunk1.content = "chunk one";
  chunk1.embedding_json = "[0.1, 0.2]";
  db.CreateKnowledgeChunk(chunk1);

  KnowledgeChunk chunk2;
  chunk2.document_id = document_id;
  chunk2.content = "chunk two";
  chunk2.embedding_json = "[0.3, 0.4]";
  db.CreateKnowledgeChunk(chunk2);

  std::vector<KnowledgeChunk> chunks = db.GetChunksForDocument(document_id);
  ASSERT_EQ(chunks.size(), 2u);
  EXPECT_EQ(chunks[0].content, "chunk one");
  EXPECT_EQ(chunks[1].content, "chunk two");
}

TEST_F(SqliteDatabaseTest, UpsertProviderCredentialUpdatesInPlace) {
  SqliteDatabase db(db_path_);

  ProviderCredential credential;
  credential.user_id = "user-1";
  credential.provider = "anthropic";
  credential.credential_value = "sk-first";

  int64_t first_id = db.UpsertProviderCredential(credential);
  EXPECT_GT(first_id, 0);

  credential.credential_value = "sk-second";
  int64_t second_id = db.UpsertProviderCredential(credential);

  EXPECT_EQ(first_id, second_id);

  auto fetched = db.GetProviderCredential("user-1", "anthropic");
  ASSERT_TRUE(fetched.has_value());
  EXPECT_EQ(fetched->credential_value, "sk-second");
  EXPECT_EQ(fetched->id, first_id);
}

TEST_F(SqliteDatabaseTest, GetProviderCredentialMissingReturnsNullopt) {
  SqliteDatabase db(db_path_);
  EXPECT_FALSE(db.GetProviderCredential("nobody", "openai").has_value());
}

TEST_F(SqliteDatabaseTest, RecordAndGetTokenUsage) {
  SqliteDatabase db(db_path_);

  Codebase codebase;
  codebase.created_at = "2026-07-10T00:00:00Z";
  int64_t codebase_id = db.CreateCodebase(codebase);

  RunModel run;
  run.codebase_id = codebase_id;
  run.status = "running";
  run.created_at = "2026-07-10T00:01:00Z";
  int64_t run_id = db.CreateRun(run);

  TokenUsageEntry entry1;
  entry1.run_id = run_id;
  entry1.subtask_key = "subtask-1";
  entry1.agent_role = "coder";
  entry1.prompt_tokens = 120;
  entry1.completion_tokens = 40;
  entry1.estimated_cost = 0.0012;
  entry1.latency_ms = 850;
  entry1.created_at = "2026-07-10T00:02:00Z";
  db.RecordTokenUsage(entry1);

  TokenUsageEntry entry2;
  entry2.run_id = run_id;
  entry2.subtask_key = "subtask-1";
  entry2.agent_role = "manager";
  entry2.prompt_tokens = 60;
  entry2.completion_tokens = 20;
  entry2.estimated_cost = 0.0005;
  entry2.latency_ms = 300;
  entry2.created_at = "2026-07-10T00:03:00Z";
  db.RecordTokenUsage(entry2);

  std::vector<TokenUsageEntry> entries = db.GetTokenUsageForRun(run_id);
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].agent_role, "coder");
  EXPECT_EQ(entries[0].prompt_tokens, 120);
  EXPECT_DOUBLE_EQ(entries[0].estimated_cost, 0.0012);
  EXPECT_EQ(entries[1].agent_role, "manager");
  EXPECT_EQ(entries[1].latency_ms, 300);
}

TEST_F(SqliteDatabaseTest, DataPersistsAcrossReopen) {
  int64_t run_id = 0;
  {
    SqliteDatabase db(db_path_);
    Codebase codebase;
    codebase.name = "persisted";
    codebase.created_at = "2026-07-10T00:00:00Z";
    int64_t codebase_id = db.CreateCodebase(codebase);

    RunModel run;
    run.codebase_id = codebase_id;
    run.status = "running";
    run.created_at = "2026-07-10T00:01:00Z";
    run_id = db.CreateRun(run);
  }

  SqliteDatabase reopened(db_path_);
  auto fetched = reopened.GetRun(run_id);
  ASSERT_TRUE(fetched.has_value());
  EXPECT_EQ(fetched->status, "running");

  auto codebase = reopened.GetCodebase(fetched->codebase_id);
  ASSERT_TRUE(codebase.has_value());
  EXPECT_EQ(codebase->name, "persisted");
}
