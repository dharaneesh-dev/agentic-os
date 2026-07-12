#pragma once

#include "cooper/core/llm/provider.hpp"
#include "cooper/core/memory/usage_ledger.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace cooper::core::orchestrator {

// Wraps a real LlmProvider purely to record one TokenUsageEntry per Chat() call into the real
// UsageLedger, tagged with whatever SetContext was last called with.
class TrackingProvider : public llm::LlmProvider {
 public:
  TrackingProvider(llm::LlmProvider& inner, memory::UsageLedger& ledger, int64_t run_id);

  void SetContext(const std::string& subtask_key, const std::string& agent_role);

  llm::ChatResult Chat(const std::vector<llm::ChatMessage>& messages,
                        const std::vector<llm::ToolDefinition>& tools) override;
  bool SupportsToolCalling() const override;
  std::string Name() const override;

 private:
  llm::LlmProvider& inner_;
  memory::UsageLedger& ledger_;
  int64_t run_id_;
  std::string current_subtask_key_;
  std::string current_agent_role_;
};

}  // namespace cooper::core::orchestrator
