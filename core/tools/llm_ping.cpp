#include "cooper/core/llm/provider_factory.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

using cooper::core::llm::ChatMessage;
using cooper::core::llm::ChatResult;
using cooper::core::llm::CreateProvider;
using cooper::core::llm::ProviderConfig;

namespace {

struct ParsedArgs {
  ProviderConfig config;
  std::string message;
};

std::optional<ParsedArgs> ParseArgs(int argc, char** argv) {
  ParsedArgs parsed;
  std::vector<std::string> positional;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto next_value = [&]() -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error("missing value for " + arg);
      }
      return argv[++i];
    };

    if (arg == "--provider") {
      parsed.config.provider_name = next_value();
    } else if (arg == "--base-url") {
      parsed.config.base_url = next_value();
    } else if (arg == "--model") {
      parsed.config.model = next_value();
    } else if (arg == "--api-key") {
      parsed.config.api_key = next_value();
    } else {
      positional.push_back(arg);
    }
  }

  if (parsed.config.provider_name.empty() || parsed.config.base_url.empty() || parsed.config.model.empty() ||
      positional.size() != 1) {
    return std::nullopt;
  }
  parsed.message = positional.front();
  return parsed;
}

void PrintUsage() {
  std::cerr << "usage: cooper_llm_ping --provider <p> --base-url <u> --model <m> [--api-key <k>] "
               "\"<message>\"\n";
}

}  // namespace

int main(int argc, char** argv) {
  auto parsed = ParseArgs(argc, argv);
  if (!parsed) {
    PrintUsage();
    return 1;
  }

  auto provider = CreateProvider(parsed->config);

  ChatMessage user_message;
  user_message.role = "user";
  user_message.content = parsed->message;

  ChatResult result = provider->Chat({user_message}, {});

  std::cout << "Provider: " << provider->Name() << "\n";
  std::cout << "Response: " << result.text << "\n";
  std::cout << "Prompt tokens: " << result.prompt_tokens << "\n";
  std::cout << "Completion tokens: " << result.completion_tokens << "\n";
  return 0;
}
