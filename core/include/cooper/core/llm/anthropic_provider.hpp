#pragma once

#include "cooper/core/embeddings/embedding_provider.hpp"
#include "cooper/core/llm/provider.hpp"
#include "cooper/core/llm/provider_config.hpp"

namespace cooper::core::llm {

class AnthropicProvider : public LlmProvider, public embeddings::EmbeddingProvider {
 public:
  explicit AnthropicProvider(ProviderConfig config);

  ChatResult Chat(const std::vector<ChatMessage>& messages, const std::vector<ToolDefinition>& tools) override;
  bool SupportsToolCalling() const override;
  std::string Name() const override;

  // anthropic has no first-party embeddings endpoint. Both throw std::runtime_error rather than
  // faking a result.
  std::vector<float> Embed(const std::string& text) override;
  size_t Dimension() const override;

 private:
  ProviderConfig config_;
};

}  // namespace cooper::core::llm
