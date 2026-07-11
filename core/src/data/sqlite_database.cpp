#include "cooper/core/data/sqlite_database.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <utility>

namespace cooper::core::data {

namespace {

struct Sqlite3StmtDeleter {
  void operator()(sqlite3_stmt* stmt) const { sqlite3_finalize(stmt); }
};
using StatementPtr = std::unique_ptr<sqlite3_stmt, Sqlite3StmtDeleter>;

void ThrowIfError(int rc, sqlite3* db, const std::string& context) {
  if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE) {
    throw std::runtime_error(context + ": " + sqlite3_errmsg(db));
  }
}

StatementPtr Prepare(sqlite3* db, const std::string& sql) {
  sqlite3_stmt* raw = nullptr;
  int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr);
  ThrowIfError(rc, db, "prepare: " + sql);
  return StatementPtr(raw);
}

void BindText(sqlite3_stmt* stmt, int index, const std::string& value) {
  sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

void BindInt64(sqlite3_stmt* stmt, int index, int64_t value) { sqlite3_bind_int64(stmt, index, value); }

void BindInt(sqlite3_stmt* stmt, int index, int value) { sqlite3_bind_int(stmt, index, value); }

void BindDouble(sqlite3_stmt* stmt, int index, double value) { sqlite3_bind_double(stmt, index, value); }

std::string ColumnText(sqlite3_stmt* stmt, int index) {
  const unsigned char* text = sqlite3_column_text(stmt, index);
  return text != nullptr ? std::string(reinterpret_cast<const char*>(text)) : std::string();
}

constexpr const char* kPragmas = R"sql(
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
PRAGMA busy_timeout = 5000;
)sql";

constexpr const char* kSchema = R"sql(
CREATE TABLE IF NOT EXISTS codebases (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  repo_path TEXT NOT NULL,
  created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS runs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  codebase_id INTEGER NOT NULL REFERENCES codebases(id),
  business_requirement TEXT NOT NULL,
  status TEXT NOT NULL,
  created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_runs_codebase_id ON runs(codebase_id);

CREATE TABLE IF NOT EXISTS run_events (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  run_id INTEGER NOT NULL REFERENCES runs(id),
  event_type TEXT NOT NULL,
  data_json TEXT NOT NULL,
  created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_run_events_run_id ON run_events(run_id);

CREATE TABLE IF NOT EXISTS subtasks (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  run_id INTEGER NOT NULL REFERENCES runs(id),
  subtask_key TEXT NOT NULL,
  description TEXT NOT NULL,
  status TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_subtasks_run_id ON subtasks(run_id);

CREATE TABLE IF NOT EXISTS subtask_attempts (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  subtask_id INTEGER NOT NULL REFERENCES subtasks(id),
  attempt_number INTEGER NOT NULL,
  status TEXT NOT NULL,
  created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_subtask_attempts_subtask_id ON subtask_attempts(subtask_id);

CREATE TABLE IF NOT EXISTS knowledge_documents (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  codebase_id INTEGER NOT NULL REFERENCES codebases(id),
  source TEXT NOT NULL,
  title TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_knowledge_documents_codebase_id ON knowledge_documents(codebase_id);

CREATE TABLE IF NOT EXISTS knowledge_chunks (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  document_id INTEGER NOT NULL REFERENCES knowledge_documents(id),
  content TEXT NOT NULL,
  embedding_json TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_knowledge_chunks_document_id ON knowledge_chunks(document_id);

CREATE TABLE IF NOT EXISTS provider_credentials (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id TEXT NOT NULL,
  provider TEXT NOT NULL,
  credential_value TEXT NOT NULL,
  UNIQUE(user_id, provider)
);

CREATE TABLE IF NOT EXISTS token_usage (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  run_id INTEGER NOT NULL REFERENCES runs(id),
  subtask_key TEXT NOT NULL,
  agent_role TEXT NOT NULL,
  prompt_tokens INTEGER NOT NULL,
  completion_tokens INTEGER NOT NULL,
  estimated_cost REAL NOT NULL,
  latency_ms INTEGER NOT NULL,
  created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_token_usage_run_id ON token_usage(run_id);
)sql";

}  // namespace

void SqliteDatabase::Sqlite3Deleter::operator()(sqlite3* handle) const { sqlite3_close(handle); }

SqliteDatabase::SqliteDatabase(const std::filesystem::path& path) {
  sqlite3* raw = nullptr;
  int rc = sqlite3_open_v2(path.string().c_str(), &raw,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
  db_.reset(raw);
  if (rc != SQLITE_OK) {
    throw std::runtime_error(std::string("SqliteDatabase::SqliteDatabase: ") +
                              (raw != nullptr ? sqlite3_errmsg(raw) : "failed to open database"));
  }
  // PRAGMA foreign_keys/journal_mode are per-connection, not persisted in the
  // file, so they must be re-applied on every open, not just the first.
  ExecOrThrow(kPragmas, "SqliteDatabase::SqliteDatabase pragmas");
  InitializeSchema();
}

void SqliteDatabase::ExecOrThrow(const char* sql, const std::string& context) {
  char* err = nullptr;
  int rc = sqlite3_exec(db_.get(), sql, nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    std::string message = err != nullptr ? err : "unknown error";
    sqlite3_free(err);
    throw std::runtime_error(context + ": " + message);
  }
}

void SqliteDatabase::InitializeSchema() { ExecOrThrow(kSchema, "SqliteDatabase::InitializeSchema"); }

int64_t SqliteDatabase::CreateCodebase(const Codebase& codebase) {
  auto stmt = Prepare(db_.get(), "INSERT INTO codebases (name, repo_path, created_at) VALUES (?, ?, ?)");
  BindText(stmt.get(), 1, codebase.name);
  BindText(stmt.get(), 2, codebase.repo_path);
  BindText(stmt.get(), 3, codebase.created_at);
  ThrowIfError(sqlite3_step(stmt.get()), db_.get(), "CreateCodebase");
  return sqlite3_last_insert_rowid(db_.get());
}

std::optional<Codebase> SqliteDatabase::GetCodebase(int64_t id) {
  auto stmt = Prepare(db_.get(), "SELECT id, name, repo_path, created_at FROM codebases WHERE id = ?");
  BindInt64(stmt.get(), 1, id);
  int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) return std::nullopt;
  ThrowIfError(rc, db_.get(), "GetCodebase");

  Codebase result;
  result.id = sqlite3_column_int64(stmt.get(), 0);
  result.name = ColumnText(stmt.get(), 1);
  result.repo_path = ColumnText(stmt.get(), 2);
  result.created_at = ColumnText(stmt.get(), 3);
  return result;
}

int64_t SqliteDatabase::CreateRun(const Run& run) {
  auto stmt = Prepare(
      db_.get(), "INSERT INTO runs (codebase_id, business_requirement, status, created_at) VALUES (?, ?, ?, ?)");
  BindInt64(stmt.get(), 1, run.codebase_id);
  BindText(stmt.get(), 2, run.business_requirement);
  BindText(stmt.get(), 3, run.status);
  BindText(stmt.get(), 4, run.created_at);
  ThrowIfError(sqlite3_step(stmt.get()), db_.get(), "CreateRun");
  return sqlite3_last_insert_rowid(db_.get());
}

std::optional<Run> SqliteDatabase::GetRun(int64_t id) {
  auto stmt =
      Prepare(db_.get(), "SELECT id, codebase_id, business_requirement, status, created_at FROM runs WHERE id = ?");
  BindInt64(stmt.get(), 1, id);
  int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) return std::nullopt;
  ThrowIfError(rc, db_.get(), "GetRun");

  Run result;
  result.id = sqlite3_column_int64(stmt.get(), 0);
  result.codebase_id = sqlite3_column_int64(stmt.get(), 1);
  result.business_requirement = ColumnText(stmt.get(), 2);
  result.status = ColumnText(stmt.get(), 3);
  result.created_at = ColumnText(stmt.get(), 4);
  return result;
}

void SqliteDatabase::UpdateRunStatus(int64_t run_id, const std::string& status) {
  auto stmt = Prepare(db_.get(), "UPDATE runs SET status = ? WHERE id = ?");
  BindText(stmt.get(), 1, status);
  BindInt64(stmt.get(), 2, run_id);
  ThrowIfError(sqlite3_step(stmt.get()), db_.get(), "UpdateRunStatus");
}

int64_t SqliteDatabase::AppendRunEvent(const RunEvent& event) {
  auto stmt =
      Prepare(db_.get(), "INSERT INTO run_events (run_id, event_type, data_json, created_at) VALUES (?, ?, ?, ?)");
  BindInt64(stmt.get(), 1, event.run_id);
  BindText(stmt.get(), 2, event.event_type);
  BindText(stmt.get(), 3, event.data_json);
  BindText(stmt.get(), 4, event.created_at);
  ThrowIfError(sqlite3_step(stmt.get()), db_.get(), "AppendRunEvent");
  return sqlite3_last_insert_rowid(db_.get());
}

std::vector<RunEvent> SqliteDatabase::GetRunEvents(int64_t run_id) {
  auto stmt = Prepare(
      db_.get(),
      "SELECT id, run_id, event_type, data_json, created_at FROM run_events WHERE run_id = ? ORDER BY id ASC");
  BindInt64(stmt.get(), 1, run_id);

  std::vector<RunEvent> events;
  int rc;
  while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
    RunEvent event;
    event.id = sqlite3_column_int64(stmt.get(), 0);
    event.run_id = sqlite3_column_int64(stmt.get(), 1);
    event.event_type = ColumnText(stmt.get(), 2);
    event.data_json = ColumnText(stmt.get(), 3);
    event.created_at = ColumnText(stmt.get(), 4);
    events.push_back(std::move(event));
  }
  ThrowIfError(rc, db_.get(), "GetRunEvents");
  return events;
}

int64_t SqliteDatabase::CreateSubtask(const Subtask& subtask) {
  auto stmt =
      Prepare(db_.get(), "INSERT INTO subtasks (run_id, subtask_key, description, status) VALUES (?, ?, ?, ?)");
  BindInt64(stmt.get(), 1, subtask.run_id);
  BindText(stmt.get(), 2, subtask.subtask_key);
  BindText(stmt.get(), 3, subtask.description);
  BindText(stmt.get(), 4, subtask.status);
  ThrowIfError(sqlite3_step(stmt.get()), db_.get(), "CreateSubtask");
  return sqlite3_last_insert_rowid(db_.get());
}

std::vector<Subtask> SqliteDatabase::GetSubtasksForRun(int64_t run_id) {
  auto stmt = Prepare(
      db_.get(), "SELECT id, run_id, subtask_key, description, status FROM subtasks WHERE run_id = ? ORDER BY id ASC");
  BindInt64(stmt.get(), 1, run_id);

  std::vector<Subtask> subtasks;
  int rc;
  while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
    Subtask subtask;
    subtask.id = sqlite3_column_int64(stmt.get(), 0);
    subtask.run_id = sqlite3_column_int64(stmt.get(), 1);
    subtask.subtask_key = ColumnText(stmt.get(), 2);
    subtask.description = ColumnText(stmt.get(), 3);
    subtask.status = ColumnText(stmt.get(), 4);
    subtasks.push_back(std::move(subtask));
  }
  ThrowIfError(rc, db_.get(), "GetSubtasksForRun");
  return subtasks;
}

void SqliteDatabase::UpdateSubtaskStatus(int64_t subtask_id, const std::string& status) {
  auto stmt = Prepare(db_.get(), "UPDATE subtasks SET status = ? WHERE id = ?");
  BindText(stmt.get(), 1, status);
  BindInt64(stmt.get(), 2, subtask_id);
  ThrowIfError(sqlite3_step(stmt.get()), db_.get(), "UpdateSubtaskStatus");
}

int64_t SqliteDatabase::CreateSubtaskAttempt(const SubtaskAttempt& attempt) {
  auto stmt = Prepare(
      db_.get(), "INSERT INTO subtask_attempts (subtask_id, attempt_number, status, created_at) VALUES (?, ?, ?, ?)");
  BindInt64(stmt.get(), 1, attempt.subtask_id);
  BindInt(stmt.get(), 2, attempt.attempt_number);
  BindText(stmt.get(), 3, attempt.status);
  BindText(stmt.get(), 4, attempt.created_at);
  ThrowIfError(sqlite3_step(stmt.get()), db_.get(), "CreateSubtaskAttempt");
  return sqlite3_last_insert_rowid(db_.get());
}

std::vector<SubtaskAttempt> SqliteDatabase::GetAttemptsForSubtask(int64_t subtask_id) {
  auto stmt = Prepare(db_.get(),
                       "SELECT id, subtask_id, attempt_number, status, created_at FROM subtask_attempts "
                       "WHERE subtask_id = ? ORDER BY id ASC");
  BindInt64(stmt.get(), 1, subtask_id);

  std::vector<SubtaskAttempt> attempts;
  int rc;
  while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
    SubtaskAttempt attempt;
    attempt.id = sqlite3_column_int64(stmt.get(), 0);
    attempt.subtask_id = sqlite3_column_int64(stmt.get(), 1);
    attempt.attempt_number = sqlite3_column_int(stmt.get(), 2);
    attempt.status = ColumnText(stmt.get(), 3);
    attempt.created_at = ColumnText(stmt.get(), 4);
    attempts.push_back(std::move(attempt));
  }
  ThrowIfError(rc, db_.get(), "GetAttemptsForSubtask");
  return attempts;
}

int64_t SqliteDatabase::CreateKnowledgeDocument(const KnowledgeDocument& document) {
  auto stmt = Prepare(db_.get(), "INSERT INTO knowledge_documents (codebase_id, source, title) VALUES (?, ?, ?)");
  BindInt64(stmt.get(), 1, document.codebase_id);
  BindText(stmt.get(), 2, document.source);
  BindText(stmt.get(), 3, document.title);
  ThrowIfError(sqlite3_step(stmt.get()), db_.get(), "CreateKnowledgeDocument");
  return sqlite3_last_insert_rowid(db_.get());
}

int64_t SqliteDatabase::CreateKnowledgeChunk(const KnowledgeChunk& chunk) {
  auto stmt =
      Prepare(db_.get(), "INSERT INTO knowledge_chunks (document_id, content, embedding_json) VALUES (?, ?, ?)");
  BindInt64(stmt.get(), 1, chunk.document_id);
  BindText(stmt.get(), 2, chunk.content);
  BindText(stmt.get(), 3, chunk.embedding_json);
  ThrowIfError(sqlite3_step(stmt.get()), db_.get(), "CreateKnowledgeChunk");
  return sqlite3_last_insert_rowid(db_.get());
}

std::vector<KnowledgeChunk> SqliteDatabase::GetChunksForDocument(int64_t document_id) {
  auto stmt = Prepare(db_.get(),
                       "SELECT id, document_id, content, embedding_json FROM knowledge_chunks "
                       "WHERE document_id = ? ORDER BY id ASC");
  BindInt64(stmt.get(), 1, document_id);

  std::vector<KnowledgeChunk> chunks;
  int rc;
  while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
    KnowledgeChunk chunk;
    chunk.id = sqlite3_column_int64(stmt.get(), 0);
    chunk.document_id = sqlite3_column_int64(stmt.get(), 1);
    chunk.content = ColumnText(stmt.get(), 2);
    chunk.embedding_json = ColumnText(stmt.get(), 3);
    chunks.push_back(std::move(chunk));
  }
  ThrowIfError(rc, db_.get(), "GetChunksForDocument");
  return chunks;
}

int64_t SqliteDatabase::UpsertProviderCredential(const ProviderCredential& credential) {
  auto stmt = Prepare(db_.get(),
                       "INSERT INTO provider_credentials (user_id, provider, credential_value) VALUES (?, ?, ?) "
                       "ON CONFLICT(user_id, provider) DO UPDATE SET credential_value = excluded.credential_value");
  BindText(stmt.get(), 1, credential.user_id);
  BindText(stmt.get(), 2, credential.provider);
  BindText(stmt.get(), 3, credential.credential_value);
  ThrowIfError(sqlite3_step(stmt.get()), db_.get(), "UpsertProviderCredential");

  // last_insert_rowid() is unreliable when the conflict branch fires, so look the id up explicitly.
  auto select_stmt = Prepare(db_.get(), "SELECT id FROM provider_credentials WHERE user_id = ? AND provider = ?");
  BindText(select_stmt.get(), 1, credential.user_id);
  BindText(select_stmt.get(), 2, credential.provider);
  int rc = sqlite3_step(select_stmt.get());
  ThrowIfError(rc, db_.get(), "UpsertProviderCredential select id");
  return sqlite3_column_int64(select_stmt.get(), 0);
}

std::optional<ProviderCredential> SqliteDatabase::GetProviderCredential(const std::string& user_id,
                                                                         const std::string& provider) {
  auto stmt = Prepare(
      db_.get(), "SELECT id, user_id, provider, credential_value FROM provider_credentials "
                 "WHERE user_id = ? AND provider = ?");
  BindText(stmt.get(), 1, user_id);
  BindText(stmt.get(), 2, provider);
  int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) return std::nullopt;
  ThrowIfError(rc, db_.get(), "GetProviderCredential");

  ProviderCredential result;
  result.id = sqlite3_column_int64(stmt.get(), 0);
  result.user_id = ColumnText(stmt.get(), 1);
  result.provider = ColumnText(stmt.get(), 2);
  result.credential_value = ColumnText(stmt.get(), 3);
  return result;
}

int64_t SqliteDatabase::RecordTokenUsage(const TokenUsageEntry& entry) {
  auto stmt = Prepare(db_.get(),
                       "INSERT INTO token_usage (run_id, subtask_key, agent_role, prompt_tokens, completion_tokens, "
                       "estimated_cost, latency_ms, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
  BindInt64(stmt.get(), 1, entry.run_id);
  BindText(stmt.get(), 2, entry.subtask_key);
  BindText(stmt.get(), 3, entry.agent_role);
  BindInt(stmt.get(), 4, entry.prompt_tokens);
  BindInt(stmt.get(), 5, entry.completion_tokens);
  BindDouble(stmt.get(), 6, entry.estimated_cost);
  BindInt64(stmt.get(), 7, entry.latency_ms);
  BindText(stmt.get(), 8, entry.created_at);
  ThrowIfError(sqlite3_step(stmt.get()), db_.get(), "RecordTokenUsage");
  return sqlite3_last_insert_rowid(db_.get());
}

std::vector<TokenUsageEntry> SqliteDatabase::GetTokenUsageForRun(int64_t run_id) {
  auto stmt = Prepare(db_.get(),
                       "SELECT id, run_id, subtask_key, agent_role, prompt_tokens, completion_tokens, "
                       "estimated_cost, latency_ms, created_at FROM token_usage WHERE run_id = ? ORDER BY id ASC");
  BindInt64(stmt.get(), 1, run_id);

  std::vector<TokenUsageEntry> entries;
  int rc;
  while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
    TokenUsageEntry entry;
    entry.id = sqlite3_column_int64(stmt.get(), 0);
    entry.run_id = sqlite3_column_int64(stmt.get(), 1);
    entry.subtask_key = ColumnText(stmt.get(), 2);
    entry.agent_role = ColumnText(stmt.get(), 3);
    entry.prompt_tokens = sqlite3_column_int(stmt.get(), 4);
    entry.completion_tokens = sqlite3_column_int(stmt.get(), 5);
    entry.estimated_cost = sqlite3_column_double(stmt.get(), 6);
    entry.latency_ms = sqlite3_column_int64(stmt.get(), 7);
    entry.created_at = ColumnText(stmt.get(), 8);
    entries.push_back(std::move(entry));
  }
  ThrowIfError(rc, db_.get(), "GetTokenUsageForRun");
  return entries;
}

}  // namespace cooper::core::data
