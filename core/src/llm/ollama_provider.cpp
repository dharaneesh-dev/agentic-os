#include "cooper/core/llm/ollama_provider.hpp"

#include "http_util.hpp"

#include <stdexcept>

namespace cooper::core::llm {

namespace {

nlohmann::json BuildMessageJson(const ChatMessage& message) {
  if (message.role == "tool") {
    // Ollama has no tool_call_id correlation field on tool-result messages -- it matches by
    // tool_name alone (see https://github.com/ollama/ollama/blob/main/docs/api.md).
    return nlohmann::json{{"role", "tool"}, {"content", message.content}, {"tool_name", message.tool_name}};
  }

  nlohmann::json json_message = {{"role", message.role}, {"content", message.content}};
  if (!message.tool_calls.empty()) {
    nlohmann::json tool_calls = nlohmann::json::array();
    for (const auto& call : message.tool_calls) {
      nlohmann::json arguments = detail::ParseJsonSchemaOrEmptyObject(call.arguments_json);
      tool_calls.push_back({{"function", {{"name", call.tool_name}, {"arguments", arguments}}}});
    }
    json_message["tool_calls"] = tool_calls;
  }
  return json_message;
}

nlohmann::json BuildToolJson(const ToolDefinition& tool) {
  return nlohmann::json{
      {"type", "function"},
      {"function",
       {{"name", tool.name},
        {"description", tool.description},
        {"parameters", detail::ParseJsonSchemaOrEmptyObject(tool.parameters_json_schema)}}}};
}

}  // namespace

OllamaProvider::OllamaProvider(ProviderConfig config) : config_(std::move(config)) {}

std::string OllamaProvider::Name() const { return "ollama"; }

bool OllamaProvider::SupportsToolCalling() const { return config_.assume_tool_calling_supported; }

ChatResult OllamaProvider::Chat(const std::vector<ChatMessage>& messages, const std::vector<ToolDefinition>& tools) {
  if (!tools.empty() && !SupportsToolCalling()) {
    throw std::runtime_error(
        "OllamaProvider::Chat: tools were requested but assume_tool_calling_supported is false; "
        "set it explicitly once the configured model is known to support tool calling");
  }

  nlohmann::json messages_json = nlohmann::json::array();
  for (const auto& message : messages) {
    messages_json.push_back(BuildMessageJson(message));
  }

  nlohmann::json body = {{"model", config_.model}, {"messages", messages_json}, {"stream", false}};
  if (!tools.empty()) {
    nlohmann::json tools_json = nlohmann::json::array();
    for (const auto& tool : tools) {
      tools_json.push_back(BuildToolJson(tool));
    }
    body["tools"] = tools_json;
  }

  auto client = detail::MakeClient(config_.base_url, config_.timeout_seconds);
  nlohmann::json response = detail::PostJson(*client, "/api/chat", httplib::Headers{}, body, "OllamaProvider::Chat");

  ChatResult result;
  const nlohmann::json& response_message = response.at("message");
  result.text = response_message.value("content", "");

  if (response_message.contains("tool_calls")) {
    const nlohmann::json& tool_calls = response_message.at("tool_calls");
    int index = 0;
    for (const auto& call : tool_calls) {
      ToolCallRequest request;
      request.id = "ollama-call-" + std::to_string(index++);
      request.tool_name = call.at("function").at("name").get<std::string>();
      request.arguments_json = call.at("function").at("arguments").dump();
      result.tool_calls.push_back(std::move(request));
    }
    result.has_tool_call = !result.tool_calls.empty();
  }

  result.prompt_tokens = response.value("prompt_eval_count", 0);
  result.completion_tokens = response.value("eval_count", 0);
  return result;
}

std::vector<float> OllamaProvider::Embed(const std::string& text) {
  nlohmann::json body = {{"model", config_.model}, {"prompt", text}};
  auto client = detail::MakeClient(config_.base_url, config_.timeout_seconds);
  nlohmann::json response =
      detail::PostJson(*client, "/api/embeddings", httplib::Headers{}, body, "OllamaProvider::Embed");

  std::vector<float> embedding;
  for (const auto& value : response.at("embedding")) {
    embedding.push_back(value.get<float>());
  }
  embedding_dim_ = embedding.size();
  return embedding;
}

size_t OllamaProvider::Dimension() const {
  if (embedding_dim_ == 0) {
    // Ollama has no static "what dimension does this model embed to" endpoint -- determine it by
    // probing with a short embed call once, then cache the result.
    const_cast<OllamaProvider*>(this)->Embed("dimension probe");
  }
  return embedding_dim_;
}

}  // namespace cooper::core::llm
