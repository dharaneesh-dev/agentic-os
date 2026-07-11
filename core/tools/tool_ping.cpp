#include "cooper/core/llm/provider_factory.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using cooper::core::llm::ChatMessage;
using cooper::core::llm::ChatResult;
using cooper::core::llm::CreateProvider;
using cooper::core::llm::ProviderConfig;
using cooper::core::llm::ToolDefinition;

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
  std::cerr << "usage: cooper_tool_ping --provider <p> --base-url <u> --model <m> [--api-key <k>] "
               "\"<message>\"\n";
}

ToolDefinition GetCurrentTimeTool() {
  ToolDefinition tool;
  tool.name = "get_current_time";
  tool.description = "Returns the current time. Takes no arguments.";
  tool.parameters_json_schema = R"({"type":"object","properties":{}})";
  return tool;
}

std::string ExecuteTool(const std::string& tool_name) {
  if (tool_name == "get_current_time") {
    return "2026-07-11T00:00:00Z";
  }
  return "error: unknown tool '" + tool_name + "'";
}

}  // namespace

int main(int argc, char** argv) {
  auto parsed = ParseArgs(argc, argv);
  if (!parsed) {
    PrintUsage();
    return 1;
  }

  // The whole point of this tool is to exercise tool calling, so declare it supported
  // unconditionally -- Chat() would otherwise refuse to send the tool definition at all.
  parsed->config.assume_tool_calling_supported = true;

  auto provider = CreateProvider(parsed->config);
  std::vector<ToolDefinition> tools = {GetCurrentTimeTool()};

  std::vector<ChatMessage> messages;
  ChatMessage user_message;
  user_message.role = "user";
  user_message.content = parsed->message;
  messages.push_back(user_message);

  ChatResult result = provider->Chat(messages, tools);
  bool required_tool_round_trip = false;

  if (result.has_tool_call) {
    required_tool_round_trip = true;

    ChatMessage assistant_message;
    assistant_message.role = "assistant";
    assistant_message.content = result.text;
    assistant_message.tool_calls = result.tool_calls;
    messages.push_back(assistant_message);

    for (const auto& call : result.tool_calls) {
      ChatMessage tool_message;
      tool_message.role = "tool";
      tool_message.tool_call_id = call.id;
      tool_message.tool_name = call.tool_name;
      tool_message.content = ExecuteTool(call.tool_name);
      messages.push_back(tool_message);
    }

    result = provider->Chat(messages, tools);
  }

  std::cout << "Provider: " << provider->Name() << "\n";
  std::cout << "Required a tool round trip: " << (required_tool_round_trip ? "yes" : "no") << "\n";
  std::cout << "Final response: " << result.text << "\n";
  std::cout << "Prompt tokens: " << result.prompt_tokens << "\n";
  std::cout << "Completion tokens: " << result.completion_tokens << "\n";
  return 0;
}
