#include "cooper/core/llm/openai_provider.hpp"

#include "http_util.hpp"

#include <stdexcept>

namespace cooper::core::llm {

namespace {

nlohmann::json BuildMessageJson(const ChatMessage& message) {
  if (message.role == "tool") {
    return nlohmann::json{
        {"role", "tool"}, {"tool_call_id", message.tool_call_id}, {"content", message.content}};
  }

  nlohmann::json json_message = {{"role", message.role}};
  if (!message.tool_calls.empty()) {
    // OpenAI accepts (and some deployments require) a null content when the assistant turn is
    // pure tool calls with no accompanying text.
    if (message.content.empty()) {
      json_message["content"] = nullptr;
    } else {
      json_message["content"] = message.content;
    }
    nlohmann::json tool_calls = nlohmann::json::array();
    for (const auto& call : message.tool_calls) {
      tool_calls.push_back({{"id", call.id},
                             {"type", "function"},
                             {"function", {{"name", call.tool_name}, {"arguments", call.arguments_json}}}});
    }
    json_message["tool_calls"] = tool_calls;
  } else {
    json_message["content"] = message.content;
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

OpenAiProvider::OpenAiProvider(ProviderConfig config) : config_(std::move(config)) {}

std::string OpenAiProvider::Name() const { return "openai"; }

bool OpenAiProvider::SupportsToolCalling() const { return config_.assume_tool_calling_supported; }

ChatResult OpenAiProvider::Chat(const std::vector<ChatMessage>& messages, const std::vector<ToolDefinition>& tools) {
  if (!tools.empty() && !SupportsToolCalling()) {
    throw std::runtime_error(
        "OpenAiProvider::Chat: tools were requested but assume_tool_calling_supported is false; "
        "set it explicitly once the configured model is known to support tool calling");
  }

  nlohmann::json messages_json = nlohmann::json::array();
  for (const auto& message : messages) {
    messages_json.push_back(BuildMessageJson(message));
  }

  nlohmann::json body = {{"model", config_.model}, {"messages", messages_json}};
  if (!tools.empty()) {
    nlohmann::json tools_json = nlohmann::json::array();
    for (const auto& tool : tools) {
      tools_json.push_back(BuildToolJson(tool));
    }
    body["tools"] = tools_json;
  }

  httplib::Headers headers = {{"Authorization", "Bearer " + config_.api_key}};
  auto client = detail::MakeClient(config_.base_url, config_.timeout_seconds);
  nlohmann::json response =
      detail::PostJson(*client, "/v1/chat/completions", headers, body, "OpenAiProvider::Chat");

  ChatResult result;
  const nlohmann::json& response_message = response.at("choices").at(0).at("message");

  if (response_message.contains("content") && !response_message.at("content").is_null()) {
    result.text = response_message.at("content").get<std::string>();
  }

  if (response_message.contains("tool_calls")) {
    for (const auto& call : response_message.at("tool_calls")) {
      ToolCallRequest request;
      request.id = call.at("id").get<std::string>();
      request.tool_name = call.at("function").at("name").get<std::string>();
      request.arguments_json = call.at("function").at("arguments").get<std::string>();
      result.tool_calls.push_back(std::move(request));
    }
    result.has_tool_call = !result.tool_calls.empty();
  }

  if (response.contains("usage")) {
    result.prompt_tokens = response.at("usage").value("prompt_tokens", 0);
    result.completion_tokens = response.at("usage").value("completion_tokens", 0);
  }
  return result;
}

std::vector<float> OpenAiProvider::Embed(const std::string& text) {
  nlohmann::json body = {{"model", config_.model}, {"input", text}};
  httplib::Headers headers = {{"Authorization", "Bearer " + config_.api_key}};
  auto client = detail::MakeClient(config_.base_url, config_.timeout_seconds);
  nlohmann::json response = detail::PostJson(*client, "/v1/embeddings", headers, body, "OpenAiProvider::Embed");

  std::vector<float> embedding;
  for (const auto& value : response.at("data").at(0).at("embedding")) {
    embedding.push_back(value.get<float>());
  }
  embedding_dim_ = embedding.size();
  return embedding;
}

size_t OpenAiProvider::Dimension() const {
  if (embedding_dim_ == 0) {
    const_cast<OpenAiProvider*>(this)->Embed("dimension probe");
  }
  return embedding_dim_;
}

}  // namespace cooper::core::llm
