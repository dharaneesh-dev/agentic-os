#pragma once

#include <string>

namespace cooper::core::llm {

struct ProviderConfig {
  std::string provider_name;// "ollama" | "openai" | "anthropic" | "gemini"
  std::string base_url;
  std::string api_key; // empty for ollama
  std::string model;
  int timeout_seconds = 120;
  bool assume_tool_calling_supported = false; // explicit opt-in; see LlmProvider::SupportsToolCalling
};

}  // namespace cooper::core::llm
