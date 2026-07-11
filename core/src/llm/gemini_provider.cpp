#include "cooper/core/llm/gemini_provider.hpp"

#include "http_util.hpp"

#include <stdexcept>

namespace cooper::core::llm {

namespace {

/* gemini has no "assistant"/"tool" role vocabulary: the model's own turns are "model", and a
function result is a "user" turn containing a functionResponse part. Like Anthropic, several
consecutive internal "tool" messages must be merged into one "user" turn holding one
functionResponse part per call. */
nlohmann::json BuildContentsJson(const std::vector<ChatMessage>& messages, std::string* system_instruction) {
  nlohmann::json contents = nlohmann::json::array();
  nlohmann::json pending_function_responses = nlohmann::json::array();

  auto flush_function_responses = [&]() {
    if (!pending_function_responses.empty()) {
      contents.push_back({{"role", "user"}, {"parts", pending_function_responses}});
      pending_function_responses = nlohmann::json::array();
    }
  };

  for (const auto& message : messages) {
    if (message.role == "system") {
      if (!system_instruction->empty()) {
        *system_instruction += "\n";
      }
      *system_instruction += message.content;
      continue;
    }

    if (message.role == "tool") {
      /* gemini's functionResponse.response field must be a JSON object; our tool-result content
      is a plain string, so wrap it. */
      pending_function_responses.push_back(
          {{"functionResponse", {{"name", message.tool_name}, {"response", {{"result", message.content}}}}}});
      continue;
    }

    flush_function_responses();

    const std::string role = message.role == "assistant" ? "model" : "user";
    nlohmann::json parts = nlohmann::json::array();
    if (!message.content.empty()) {
      parts.push_back({{"text", message.content}});
    }
    for (const auto& call : message.tool_calls) {
      parts.push_back({{"functionCall",
                         {{"name", call.tool_name}, {"args", detail::ParseJsonSchemaOrEmptyObject(call.arguments_json)}}}});
    }
    contents.push_back({{"role", role}, {"parts", parts}});
  }

  flush_function_responses();
  return contents;
}

nlohmann::json BuildFunctionDeclarationJson(const ToolDefinition& tool) {
  return nlohmann::json{{"name", tool.name},
                        {"description", tool.description},
                        {"parameters", detail::ParseJsonSchemaOrEmptyObject(tool.parameters_json_schema)}};
}

}  // namespace

GeminiProvider::GeminiProvider(ProviderConfig config) : config_(std::move(config)) {}

std::string GeminiProvider::Name() const { return "gemini"; }

bool GeminiProvider::SupportsToolCalling() const { return config_.assume_tool_calling_supported; }

ChatResult GeminiProvider::Chat(const std::vector<ChatMessage>& messages, const std::vector<ToolDefinition>& tools) {
  if (!tools.empty() && !SupportsToolCalling()) {
    throw std::runtime_error(
        "GeminiProvider::Chat: tools were requested but assume_tool_calling_supported is false; "
        "set it explicitly once the configured model is known to support tool calling");
  }

  std::string system_instruction;
  nlohmann::json contents = BuildContentsJson(messages, &system_instruction);

  nlohmann::json body = {{"contents", contents}};
  if (!system_instruction.empty()) {
    body["systemInstruction"] = {{"parts", nlohmann::json::array({{{"text", system_instruction}}})}};
  }
  if (!tools.empty()) {
    nlohmann::json function_declarations = nlohmann::json::array();
    for (const auto& tool : tools) {
      function_declarations.push_back(BuildFunctionDeclarationJson(tool));
    }
    body["tools"] = nlohmann::json::array({{{"functionDeclarations", function_declarations}}});
  }

  const std::string path = "/v1beta/models/" + config_.model + ":generateContent?key=" + config_.api_key;
  auto client = detail::MakeClient(config_.base_url, config_.timeout_seconds);
  nlohmann::json response = detail::PostJson(*client, path, httplib::Headers{}, body, "GeminiProvider::Chat");

  ChatResult result;
  const nlohmann::json& parts = response.at("candidates").at(0).at("content").at("parts");
  int index = 0;
  for (const auto& part : parts) {
    if (part.contains("text")) {
      result.text += part.at("text").get<std::string>();
    } else if (part.contains("functionCall")) {
      ToolCallRequest request;
      /* gemini does not assign an id to function calls - synthesize one so downstream code can
      still correlate the eventual functionResponse to this call. */
      request.id = "gemini-call-" + std::to_string(index++);
      request.tool_name = part.at("functionCall").at("name").get<std::string>();
      request.arguments_json = part.at("functionCall").at("args").dump();
      result.tool_calls.push_back(std::move(request));
    }
  }
  result.has_tool_call = !result.tool_calls.empty();

  if (response.contains("usageMetadata")) {
    result.prompt_tokens = response.at("usageMetadata").value("promptTokenCount", 0);
    result.completion_tokens = response.at("usageMetadata").value("candidatesTokenCount", 0);
  }
  return result;
}

std::vector<float> GeminiProvider::Embed(const std::string& text) {
  nlohmann::json body = {{"content", {{"parts", nlohmann::json::array({{{"text", text}}})}}}};
  const std::string path = "/v1beta/models/" + config_.model + ":embedContent?key=" + config_.api_key;
  auto client = detail::MakeClient(config_.base_url, config_.timeout_seconds);
  nlohmann::json response = detail::PostJson(*client, path, httplib::Headers{}, body, "GeminiProvider::Embed");

  std::vector<float> embedding;
  for (const auto& value : response.at("embedding").at("values")) {
    embedding.push_back(value.get<float>());
  }
  embedding_dim_ = embedding.size();
  return embedding;
}

size_t GeminiProvider::Dimension() const {
  if (embedding_dim_ == 0) {
    const_cast<GeminiProvider*>(this)->Embed("dimension probe");
  }
  return embedding_dim_;
}

}  // namespace cooper::core::llm
