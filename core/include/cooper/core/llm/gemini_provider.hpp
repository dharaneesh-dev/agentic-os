#pragma once

#include "cooper/core/embeddings/embedding_provider.hpp"
#include "cooper/core/llm/provider.hpp"
#include "cooper/core/llm/provider_config.hpp"

namespace cooper::core::llm {

class GeminiProvider : public LlmProvider, public embeddings::EmbeddingProvider {
 public:
  explicit GeminiProvider(ProviderConfig config);

  ChatResult Chat(const std::vector<ChatMessage>& messages, const std::vector<ToolDefinition>& tools) override;
  bool SupportsToolCalling() const override;
  std::string Name() const override;

  std::vector<float> Embed(const std::string& text) override;
  size_t Dimension() const override;

 private:
  ProviderConfig config_;
  mutable size_t embedding_dim_ = 0;
};

}  // namespace cooper::core::llm
