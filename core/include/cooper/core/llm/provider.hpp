#pragma once

#include "cooper/core/llm/types.hpp"

#include <string>
#include <vector>

namespace cooper::core::llm {

class LlmProvider {
 public:
  virtual ~LlmProvider() = default;

  virtual ChatResult Chat(const std::vector<ChatMessage>& messages, const std::vector<ToolDefinition>& tools) = 0;

  /* reports whatever ProviderConfig::assume_tool_calling_supported was constructed with -- it is
  an operator-declared fact, not a runtime probe. There is no reliable, uniform way to ask an
  arbitrary configured model "can you call tools" before trying: this varies by provider and
  even by which specific model is configured (true for Ollama especially, where tool-calling
  support depends on the pulled model, not the server). Chat() throws immediately, before any
  HTTP call, if tools are requested while this returns false -- fail loud instead of silently
  sending a request the model may not honor. */
  virtual bool SupportsToolCalling() const = 0;

  virtual std::string Name() const = 0;
};

}  // namespace cooper::core::llm
