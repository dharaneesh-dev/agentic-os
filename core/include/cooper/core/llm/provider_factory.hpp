#pragma once

#include "cooper/core/llm/provider.hpp"
#include "cooper/core/llm/provider_config.hpp"

#include <memory>

namespace cooper::core::llm {

// Throws std::runtime_error if config.provider_name is not one of "ollama" | "openai" |
// "anthropic" | "gemini".
std::unique_ptr<LlmProvider> CreateProvider(const ProviderConfig& config);

}  // namespace cooper::core::llm
