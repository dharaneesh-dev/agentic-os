#include "cooper/core/data/sqlite_database.hpp"
#include "cooper/core/memory/session_cache.hpp"
#include "cooper/core/memory/usage_ledger.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>

using cooper::core::data::Codebase;
using cooper::core::data::Run;
using cooper::core::data::RunEvent;
using cooper::core::data::SqliteDatabase;
using cooper::core::data::Subtask;
using cooper::core::data::SubtaskAttempt;
using cooper::core::data::TokenUsageEntry;
using cooper::core::memory::SessionCache;
using cooper::core::memory::UsageLedger;

namespace {

std::string NowIso8601() {
  auto now = std::chrono::system_clock::now();
  std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::tm tm_utc{};
#ifdef _WIN32
  gmtime_s(&tm_utc, &now_time);
#else
  gmtime_r(&now_time, &tm_utc);
#endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
  return std::string(buffer);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: cooper_db_tool <db_path>\n";
    return 1;
  }

  std::filesystem::path db_path(argv[1]);

  std::cout << "=== Cooper Data Layer Demo ===\n";
  std::cout << "Database file: " << db_path << "\n\n";

  SqliteDatabase db(db_path);

  Codebase codebase;
  codebase.name = "cooper-core";
  codebase.repo_path = std::filesystem::current_path().string();
  codebase.created_at = NowIso8601();
  int64_t codebase_id = db.CreateCodebase(codebase);
  std::cout << "Created codebase id=" << codebase_id << " name=" << codebase.name << "\n";

  Run run;
  run.codebase_id = codebase_id;
  run.business_requirement = "Add data layer + token/context memory foundation";
  run.status = "running";
  run.created_at = NowIso8601();
  int64_t run_id = db.CreateRun(run);
  std::cout << "Created run id=" << run_id << " status=" << run.status << "\n";

  for (int i = 0; i < 3; ++i) {
    RunEvent event;
    event.run_id = run_id;
    event.event_type = "status_change";
    event.data_json = "{\"step\":" + std::to_string(i) + "}";
    event.created_at = NowIso8601();
    int64_t event_id = db.AppendRunEvent(event);
    std::cout << "Appended run event id=" << event_id << " data=" << event.data_json << "\n";
  }

  Subtask subtask;
  subtask.run_id = run_id;
  subtask.subtask_key = "subtask-1";
  subtask.description = "Implement the data layer";
  subtask.status = "pending";
  int64_t subtask_id = db.CreateSubtask(subtask);
  std::cout << "Created subtask id=" << subtask_id << " key=" << subtask.subtask_key << "\n";

  for (int i = 1; i <= 2; ++i) {
    SubtaskAttempt attempt;
    attempt.subtask_id = subtask_id;
    attempt.attempt_number = i;
    attempt.status = i == 1 ? "failed" : "succeeded";
    attempt.created_at = NowIso8601();
    int64_t attempt_id = db.CreateSubtaskAttempt(attempt);
    std::cout << "Created subtask attempt id=" << attempt_id << " attempt_number=" << attempt.attempt_number
              << " status=" << attempt.status << "\n";
  }

  for (int i = 0; i < 2; ++i) {
    TokenUsageEntry entry;
    entry.run_id = run_id;
    entry.subtask_key = "subtask-1";
    entry.agent_role = i == 0 ? "coder" : "manager";
    entry.prompt_tokens = 100 + i * 10;
    entry.completion_tokens = 50 + i * 5;
    entry.estimated_cost = 0.002 * (i + 1);
    entry.latency_ms = 1200 + i * 100;
    entry.created_at = NowIso8601();
    int64_t usage_id = db.RecordTokenUsage(entry);
    std::cout << "Recorded token usage id=" << usage_id << " role=" << entry.agent_role
              << " prompt_tokens=" << entry.prompt_tokens << " completion_tokens=" << entry.completion_tokens
              << "\n";
  }

  std::cout << "\n--- Reading everything back from " << db_path << " ---\n";

  auto fetched_codebase = db.GetCodebase(codebase_id);
  if (fetched_codebase) {
    std::cout << "Codebase: id=" << fetched_codebase->id << " name=" << fetched_codebase->name
              << " repo_path=" << fetched_codebase->repo_path << " created_at=" << fetched_codebase->created_at
              << "\n";
  }

  auto fetched_run = db.GetRun(run_id);
  if (fetched_run) {
    std::cout << "Run: id=" << fetched_run->id << " codebase_id=" << fetched_run->codebase_id
              << " status=" << fetched_run->status << " requirement=\"" << fetched_run->business_requirement
              << "\"\n";
  }

  std::cout << "Run events (ordered):\n";
  for (const auto& event : db.GetRunEvents(run_id)) {
    std::cout << "  id=" << event.id << " type=" << event.event_type << " data=" << event.data_json
               << " created_at=" << event.created_at << "\n";
  }

  std::cout << "Subtasks for run:\n";
  for (const auto& s : db.GetSubtasksForRun(run_id)) {
    std::cout << "  id=" << s.id << " key=" << s.subtask_key << " status=" << s.status
               << " description=\"" << s.description << "\"\n";
  }

  std::cout << "Subtask attempts:\n";
  for (const auto& attempt : db.GetAttemptsForSubtask(subtask_id)) {
    std::cout << "  id=" << attempt.id << " attempt_number=" << attempt.attempt_number
               << " status=" << attempt.status << " created_at=" << attempt.created_at << "\n";
  }

  std::cout << "Token usage for run:\n";
  for (const auto& usage : db.GetTokenUsageForRun(run_id)) {
    std::cout << "  id=" << usage.id << " role=" << usage.agent_role << " prompt_tokens=" << usage.prompt_tokens
               << " completion_tokens=" << usage.completion_tokens << " cost=" << usage.estimated_cost
               << " latency_ms=" << usage.latency_ms << "\n";
  }

  std::cout << "\n=== Cooper Session Cache Demo ===\n";
  std::filesystem::path cache_path = db_path.parent_path() / "session_cache.json";
  std::cout << "Session cache file: " << cache_path << "\n";

  {
    SessionCache cache = SessionCache::Load(cache_path);
    cache.SetChunkCache("subtask-1", "src/authentication.cpp", "bool ValidateSession(const Session& session);");
    cache.SetChunkCache("subtask-1", "src/rate_limiter.cpp", "bool AllowRequest(const ClientId& client);");
    cache.AppendAttempt("subtask-1", nlohmann::json{{"attempt", 1}, {"result", "failed"}});
    cache.AppendAttempt("subtask-1", nlohmann::json{{"attempt", 2}, {"result", "succeeded"}});

    UsageLedger ledger(db, cache);
    TokenUsageEntry ledger_entry;
    ledger_entry.run_id = run_id;
    ledger_entry.subtask_key = "subtask-1";
    ledger_entry.agent_role = "coder";
    ledger_entry.prompt_tokens = 200;
    ledger_entry.completion_tokens = 80;
    ledger_entry.estimated_cost = 0.0035;
    ledger_entry.latency_ms = 640;
    ledger_entry.created_at = NowIso8601();
    ledger.RecordUsage(ledger_entry);

    std::cout << "Wrote chunk cache and attempt history directly; recorded token usage via UsageLedger "
                 "(writes both the SQLite row and the cache entry in one call); total tokens so far = "
              << cache.TotalTokensForRun() << "\n";
    std::cout << "Matching SQLite rows for this run now: " << db.GetTokenUsageForRun(run_id).size() << "\n";
  }

  std::cout << "\n--- Reloading a fresh SessionCache from the same file (simulated restart) ---\n";
  SessionCache reloaded = SessionCache::Load(cache_path);

  auto auth_chunk = reloaded.GetChunkCache("subtask-1", "src/authentication.cpp");
  std::cout << "Reloaded chunk cache src/authentication.cpp: " << (auth_chunk ? *auth_chunk : "<missing>") << "\n";

  auto rate_limiter_chunk = reloaded.GetChunkCache("subtask-1", "src/rate_limiter.cpp");
  std::cout << "Reloaded chunk cache src/rate_limiter.cpp: " << (rate_limiter_chunk ? *rate_limiter_chunk : "<missing>")
            << "\n";

  std::cout << "Reloaded attempt history for subtask-1:\n";
  for (const auto& attempt : reloaded.GetAttempts("subtask-1")) {
    std::cout << "  " << attempt.dump() << "\n";
  }

  std::cout << "Reloaded total tokens for run: " << reloaded.TotalTokensForRun() << "\n";

  return 0;
}
