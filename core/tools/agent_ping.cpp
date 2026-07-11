#include "cooper/core/agent/agent_loop.hpp"
#include "cooper/core/agent/roles.hpp"
#include "cooper/core/embeddings/mock_embedding_provider.hpp"
#include "cooper/core/llm/provider_factory.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using cooper::core::agent::AgentLoop;
using cooper::core::agent::AgentLoopResult;
using cooper::core::agent::MakeCoderRole;
using cooper::core::agent::MakeManagerRole;
using cooper::core::agent::MakeProductManagerRole;
using cooper::core::agent::RoleSetup;
using cooper::core::agent::RunTestsConfig;
using cooper::core::embeddings::MockEmbeddingProvider;
using cooper::core::llm::ChatMessage;
using cooper::core::llm::CreateProvider;
using cooper::core::llm::LlmProvider;
using cooper::core::llm::ProviderConfig;
using cooper::core::llm::ToolDefinition;

namespace {

// Wraps the real provider purely to accumulate token usage across every Chat() call AgentLoop
// makes -- AgentLoopResult itself carries no token totals (by design, per the agent_loop.hpp
// contract), so this is the only place to observe them.
class TokenCountingProvider : public LlmProvider {
 public:
  explicit TokenCountingProvider(LlmProvider& inner) : inner_(inner) {}

  cooper::core::llm::ChatResult Chat(const std::vector<ChatMessage>& messages,
                                      const std::vector<ToolDefinition>& tools) override {
    cooper::core::llm::ChatResult result = inner_.Chat(messages, tools);
    total_prompt_tokens_ += result.prompt_tokens;
    total_completion_tokens_ += result.completion_tokens;
    return result;
  }

  bool SupportsToolCalling() const override { return inner_.SupportsToolCalling(); }
  std::string Name() const override { return inner_.Name(); }

  int TotalPromptTokens() const { return total_prompt_tokens_; }
  int TotalCompletionTokens() const { return total_completion_tokens_; }

 private:
  LlmProvider& inner_;
  int total_prompt_tokens_ = 0;
  int total_completion_tokens_ = 0;
};

std::vector<std::string> SplitOnWhitespace(const std::string& text) {
  std::vector<std::string> parts;
  std::istringstream stream(text);
  std::string part;
  while (stream >> part) {
    parts.push_back(part);
  }
  return parts;
}

struct ParsedArgs {
  std::string role;
  std::filesystem::path repo;
  ProviderConfig provider_config;
  std::string orchestrator_dir = COOPER_ORCHESTRATOR_DIR;
  std::string test_image = "python:3.12-slim";
  std::string test_command = "python -m pytest -q";
  std::string task;
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

    if (arg == "--role") {
      parsed.role = next_value();
    } else if (arg == "--repo") {
      parsed.repo = next_value();
    } else if (arg == "--provider") {
      parsed.provider_config.provider_name = next_value();
    } else if (arg == "--base-url") {
      parsed.provider_config.base_url = next_value();
    } else if (arg == "--model") {
      parsed.provider_config.model = next_value();
    } else if (arg == "--api-key") {
      parsed.provider_config.api_key = next_value();
    } else if (arg == "--orchestrator-dir") {
      parsed.orchestrator_dir = next_value();
    } else if (arg == "--test-image") {
      parsed.test_image = next_value();
    } else if (arg == "--test-command") {
      parsed.test_command = next_value();
    } else {
      positional.push_back(arg);
    }
  }

  bool role_valid = parsed.role == "coder" || parsed.role == "product_manager" || parsed.role == "manager";
  if (!role_valid || parsed.repo.empty() || parsed.provider_config.provider_name.empty() ||
      parsed.provider_config.base_url.empty() || parsed.provider_config.model.empty() || positional.size() != 1) {
    return std::nullopt;
  }
  parsed.task = positional.front();
  return parsed;
}

void PrintUsage() {
  std::cerr << "usage: cooper_agent_ping --role coder|product_manager|manager --repo <path> "
               "--provider <p> --base-url <u> --model <m> [--api-key <k>] "
               "[--orchestrator-dir <dir>] [--test-image <docker image>] "
               "[--test-command \"<command>\"] \"<task text>\"\n"
               "  --test-image defaults to python:3.12-slim (no pytest preinstalled)\n"
               "  --test-command defaults to \"python -m pytest -q\"\n";
}

std::string Truncate(const std::string& text, size_t max_len) {
  if (text.size() <= max_len) {
    return text;
  }
  return text.substr(0, max_len) + "...";
}

void PrintSteps(const std::vector<ChatMessage>& transcript) {
  int step_number = 0;
  for (size_t i = 0; i < transcript.size(); ++i) {
    const ChatMessage& message = transcript[i];
    if (message.role != "assistant" || message.tool_calls.empty()) {
      continue;
    }
    for (const auto& call : message.tool_calls) {
      ++step_number;
      std::cout << "Step " << step_number << ": " << call.tool_name << "(" << Truncate(call.arguments_json, 120)
                 << ")\n";
      if (call.tool_name == "finish") {
        continue;
      }
      for (size_t j = i + 1; j < transcript.size(); ++j) {
        if (transcript[j].role == "tool" && transcript[j].tool_call_id == call.id) {
          std::cout << "  -> " << Truncate(transcript[j].content, 200) << "\n";
          break;
        }
      }
    }
  }
}

RoleSetup BuildRoleSetup(const ParsedArgs& args, MockEmbeddingProvider& embedder) {
  constexpr int kSearchTokenBudget = 2000;

  RunTestsConfig run_tests_config;
  run_tests_config.test_command = SplitOnWhitespace(args.test_command);
  run_tests_config.docker_image = args.test_image;
  run_tests_config.timeout_seconds = 60;
  run_tests_config.orchestrator_dir = args.orchestrator_dir;

  if (args.role == "coder") {
    return MakeCoderRole(args.repo, embedder, kSearchTokenBudget, run_tests_config);
  }
  if (args.role == "product_manager") {
    return MakeProductManagerRole(args.repo, embedder, kSearchTokenBudget);
  }
  return MakeManagerRole(args.repo);
}

}  // namespace

int main(int argc, char** argv) {
  std::optional<ParsedArgs> parsed;
  try {
    parsed = ParseArgs(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
  if (!parsed) {
    PrintUsage();
    return 1;
  }

  // The whole point of this tool is to exercise tool calling, so declare it supported
  // unconditionally -- Chat() would otherwise refuse to send the tool definitions at all.
  parsed->provider_config.assume_tool_calling_supported = true;

  try {
    auto provider = CreateProvider(parsed->provider_config);
    TokenCountingProvider counting_provider(*provider);

    MockEmbeddingProvider embedder(64);
    RoleSetup setup = BuildRoleSetup(*parsed, embedder);

    AgentLoop loop(counting_provider, std::move(setup.tools), setup.config);
    AgentLoopResult result = loop.Run(parsed->task);

    std::cout << "Role: " << parsed->role << "\n";
    std::cout << "Provider: " << counting_provider.Name() << "\n\n";

    PrintSteps(result.transcript);

    std::cout << "\nTotal steps: " << result.total_steps << "\n";
    if (result.finished) {
      std::cout << "Finished: yes\n";
      std::cout << "finish_arguments_json: " << result.finish_arguments_json << "\n";
    } else {
      std::cout << "Finished: no (budget exhausted or model returned plain text instead of finishing)\n";
      std::cout << "last_text_response: " << result.last_text_response << "\n";
    }
    std::cout << "Prompt tokens: " << counting_provider.TotalPromptTokens() << "\n";
    std::cout << "Completion tokens: " << counting_provider.TotalCompletionTokens() << "\n";
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }

  return 0;
}
