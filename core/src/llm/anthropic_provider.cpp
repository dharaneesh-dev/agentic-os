#include "cooper/core/llm/anthropic_provider.hpp"

#include "http_util.hpp"

#include <stdexcept>

namespace cooper::core::llm {

namespace {

constexpr const char* kAnthropicVersion = "2023-06-01";
constexpr int kDefaultMaxTokens = 4096;

struct AnthropicRequestParts {
  std::string system;
  nlohmann::json messages = nlohmann::json::array();
};

/* anthropic has no "tool" role: a tool result is a user-turn content block, and every tool_result
answering the same assistant turn must be batched into a single user message. This walks the
internal ChatMessage list -- which may interleave several "tool" role entries -- and merges
consecutive tool results into one user message, exactly as a second request in a tool-use round
trip must look. */
AnthropicRequestParts BuildAnthropicRequestParts(const std::vector<ChatMessage>& messages) {
  AnthropicRequestParts parts;
  nlohmann::json pending_tool_results = nlohmann::json::array();

  auto flush_tool_results = [&]() {
    if (!pending_tool_results.empty()) {
      parts.messages.push_back({{"role", "user"}, {"content", pending_tool_results}});
      pending_tool_results = nlohmann::json::array();
    }
  };

  for (const auto& message : messages) {
    if (message.role == "system") {
      if (!parts.system.empty()) {
        parts.system += "\n";
      }
      parts.system += message.content;
      continue;
    }

    if (message.role == "tool") {
      pending_tool_results.push_back(
          {{"type", "tool_result"}, {"tool_use_id", message.tool_call_id}, {"content", message.content}});
      continue;
    }

    flush_tool_results();

    nlohmann::json content_blocks = nlohmann::json::array();
    if (!message.content.empty()) {
      content_blocks.push_back({{"type", "text"}, {"text", message.content}});
    }
    for (const auto& call : message.tool_calls) {
      content_blocks.push_back({{"type", "tool_use"},
                                 {"id", call.id},
                                 {"name", call.tool_name},
                                 {"input", detail::ParseJsonSchemaOrEmptyObject(call.arguments_json)}});
    }
    parts.messages.push_back({{"role", message.role}, {"content", content_blocks}});
  }

  flush_tool_results();
  return parts;
}

nlohmann::json BuildToolJson(const ToolDefinition& tool) {
  return nlohmann::json{{"name", tool.name},
                         {"description", tool.description},
                         {"input_schema", detail::ParseJsonSchemaOrEmptyObject(tool.parameters_json_schema)}};
}

}  // namespace

AnthropicProvider::AnthropicProvider(ProviderConfig config) : config_(std::move(config)) {}

std::string AnthropicProvider::Name() const { return "anthropic"; }

bool AnthropicProvider::SupportsToolCalling() const { return config_.assume_tool_calling_supported; }

ChatResult AnthropicProvider::Chat(const std::vector<ChatMessage>& messages,
                                   const std::vector<ToolDefinition>& tools) {
  if (!tools.empty() && !SupportsToolCalling()) {
    throw std::runtime_error(
        "AnthropicProvider::Chat: tools were requested but assume_tool_calling_supported is false; "
        "set it explicitly once the configured model is known to support tool calling");
  }

  AnthropicRequestParts parts = BuildAnthropicRequestParts(messages);

  nlohmann::json body = {
      {"model", config_.model}, {"max_tokens", kDefaultMaxTokens}, {"messages", parts.messages}};
  if (!parts.system.empty()) {
    body["system"] = parts.system;
  }
  if (!tools.empty()) {
    nlohmann::json tools_json = nlohmann::json::array();
    for (const auto& tool : tools) {
      tools_json.push_back(BuildToolJson(tool));
    }
    body["tools"] = tools_json;
  }

  httplib::Headers headers = {{"x-api-key", config_.api_key}, {"anthropic-version", kAnthropicVersion}};
  auto client = detail::MakeClient(config_.base_url, config_.timeout_seconds);
  nlohmann::json response = detail::PostJson(*client, "/v1/messages", headers, body, "AnthropicProvider::Chat");

  ChatResult result;
  for (const auto& block : response.at("content")) {
    const std::string type = block.at("type").get<std::string>();
    if (type == "text") {
      result.text += block.at("text").get<std::string>();
    } else if (type == "tool_use") {
      ToolCallRequest request;
      request.id = block.at("id").get<std::string>();
      request.tool_name = block.at("name").get<std::string>();
      request.arguments_json = block.at("input").dump();
      result.tool_calls.push_back(std::move(request));
    }
  }
  result.has_tool_call = !result.tool_calls.empty();

  if (response.contains("usage")) {
    result.prompt_tokens = response.at("usage").value("input_tokens", 0);
    result.completion_tokens = response.at("usage").value("output_tokens", 0);
  }
  return result;
}

std::vector<float> AnthropicProvider::Embed(const std::string& /*text*/) {
  throw std::runtime_error(
      "AnthropicProvider::Embed: Anthropic has no first-party embeddings endpoint; use a "
      "different provider (e.g. Ollama or OpenAI) for embeddings");
}

size_t AnthropicProvider::Dimension() const {
  throw std::runtime_error(
      "AnthropicProvider::Dimension: Anthropic has no first-party embeddings endpoint; use a "
      "different provider (e.g. Ollama or OpenAI) for embeddings");
}

}  // namespace cooper::core::llm
