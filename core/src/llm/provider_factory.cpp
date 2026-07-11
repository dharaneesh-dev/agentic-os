#include "cooper/core/llm/provider_factory.hpp"

#include "cooper/core/llm/anthropic_provider.hpp"
#include "cooper/core/llm/gemini_provider.hpp"
#include "cooper/core/llm/ollama_provider.hpp"
#include "cooper/core/llm/openai_provider.hpp"

#include <stdexcept>

namespace cooper::core::llm {

std::unique_ptr<LlmProvider> CreateProvider(const ProviderConfig& config) {
  if (config.provider_name == "ollama") {
    return std::make_unique<OllamaProvider>(config);
  }
  if (config.provider_name == "openai") {
    return std::make_unique<OpenAiProvider>(config);
  }
  if (config.provider_name == "anthropic") {
    return std::make_unique<AnthropicProvider>(config);
  }
  if (config.provider_name == "gemini") {
    return std::make_unique<GeminiProvider>(config);
  }
  throw std::runtime_error("CreateProvider: unrecognized provider_name '" + config.provider_name + "'");
}

}  // namespace cooper::core::llm
