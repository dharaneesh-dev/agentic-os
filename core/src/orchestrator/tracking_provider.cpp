#include "cooper/core/orchestrator/tracking_provider.hpp"

#include "cooper/core/data/models.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace cooper::core::orchestrator {

namespace {

std::string NowIso8601() {
  std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm utc_tm{};
#ifdef _WIN32
  gmtime_s(&utc_tm, &now);
#else
  gmtime_r(&now, &utc_tm);
#endif
  std::ostringstream stream;
  stream << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
  return stream.str();
}

}  // namespace

TrackingProvider::TrackingProvider(llm::LlmProvider& inner, memory::UsageLedger& ledger, int64_t run_id)
    : inner_(inner), ledger_(ledger), run_id_(run_id) {}

void TrackingProvider::SetContext(const std::string& subtask_key, const std::string& agent_role) {
  current_subtask_key_ = subtask_key;
  current_agent_role_ = agent_role;
}

llm::ChatResult TrackingProvider::Chat(const std::vector<llm::ChatMessage>& messages,
                                        const std::vector<llm::ToolDefinition>& tools) {
  auto start = std::chrono::steady_clock::now();
  llm::ChatResult result = inner_.Chat(messages, tools);
  auto end = std::chrono::steady_clock::now();

  data::TokenUsageEntry entry;
  entry.run_id = run_id_;
  entry.subtask_key = current_subtask_key_;
  entry.agent_role = current_agent_role_;
  entry.prompt_tokens = result.prompt_tokens;
  entry.completion_tokens = result.completion_tokens;
  entry.latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  entry.created_at = NowIso8601();
  ledger_.RecordUsage(entry);

  return result;
}

bool TrackingProvider::SupportsToolCalling() const { return inner_.SupportsToolCalling(); }

std::string TrackingProvider::Name() const { return inner_.Name(); }

}  // namespace cooper::core::orchestrator
