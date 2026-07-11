#include "cooper/core/llm/ollama_provider.hpp"

#include "llm_test_server.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atomic>

using cooper::core::llm::ChatMessage;
using cooper::core::llm::ChatResult;
using cooper::core::llm::OllamaProvider;
using cooper::core::llm::ProviderConfig;
using cooper::core::llm::ToolDefinition;
using cooper::core::llm::testing::MockServer;

TEST(OllamaProviderTest, PlainTextResponseNoTools) {
  MockServer mock_server;
  std::atomic<bool> request_shape_ok{false};

  mock_server.server().Post("/api/chat", [&](const httplib::Request& req, httplib::Response& res) {
    mock_server.RecordRequest();
    nlohmann::json body = nlohmann::json::parse(req.body);
    request_shape_ok =
        body.at("model") == "llama3.2" && body.at("stream") == false && !body.contains("tools") &&
        body.at("messages").at(0).at("role") == "user" && body.at("messages").at(0).at("content") == "hi";
    res.set_content(
        R"({"model":"llama3.2","message":{"role":"assistant","content":"Hello there"},"done":true,)"
        R"("prompt_eval_count":5,"eval_count":3})",
        "application/json");
  });

  ProviderConfig config;
  config.provider_name = "ollama";
  config.base_url = mock_server.base_url();
  config.model = "llama3.2";

  OllamaProvider provider(config);
  ChatMessage user_message;
  user_message.role = "user";
  user_message.content = "hi";

  ChatResult result = provider.Chat({user_message}, {});

  EXPECT_TRUE(request_shape_ok.load());
  EXPECT_EQ(mock_server.request_count(), 1);
  EXPECT_FALSE(result.has_tool_call);
  EXPECT_EQ(result.text, "Hello there");
  EXPECT_EQ(result.prompt_tokens, 5);
  EXPECT_EQ(result.completion_tokens, 3);
}

TEST(OllamaProviderTest, ToolCallResponse) {
  MockServer mock_server;
  std::atomic<bool> request_shape_ok{false};

  mock_server.server().Post("/api/chat", [&](const httplib::Request& req, httplib::Response& res) {
    mock_server.RecordRequest();
    nlohmann::json body = nlohmann::json::parse(req.body);
    request_shape_ok = body.at("tools").at(0).at("type") == "function" &&
                        body.at("tools").at(0).at("function").at("name") == "get_weather";
    res.set_content(
        R"({"model":"llama3.2","message":{"role":"assistant","content":"",)"
        R"("tool_calls":[{"function":{"name":"get_weather","arguments":{"city":"Tokyo"}}}]},"done":false})",
        "application/json");
  });

  ProviderConfig config;
  config.provider_name = "ollama";
  config.base_url = mock_server.base_url();
  config.model = "llama3.2";
  config.assume_tool_calling_supported = true;

  OllamaProvider provider(config);
  ToolDefinition tool;
  tool.name = "get_weather";
  tool.description = "Get the weather for a city";
  tool.parameters_json_schema =
      R"({"type":"object","properties":{"city":{"type":"string"}},"required":["city"]})";

  ChatMessage user_message;
  user_message.role = "user";
  user_message.content = "weather in tokyo?";

  ChatResult result = provider.Chat({user_message}, {tool});

  EXPECT_TRUE(request_shape_ok.load());
  ASSERT_TRUE(result.has_tool_call);
  ASSERT_EQ(result.tool_calls.size(), 1u);
  EXPECT_EQ(result.tool_calls[0].tool_name, "get_weather");
  nlohmann::json arguments = nlohmann::json::parse(result.tool_calls[0].arguments_json);
  EXPECT_EQ(arguments.at("city"), "Tokyo");
}

TEST(OllamaProviderTest, ChatWithToolsThrowsWhenToolCallingNotAssumed) {
  MockServer mock_server;
  mock_server.server().Post("/api/chat", [&](const httplib::Request&, httplib::Response& res) {
    mock_server.RecordRequest();
    res.set_content("{}", "application/json");
  });

  ProviderConfig config;
  config.provider_name = "ollama";
  config.base_url = mock_server.base_url();
  config.model = "llama3.2";
  config.assume_tool_calling_supported = false;

  OllamaProvider provider(config);
  ToolDefinition tool;
  tool.name = "get_weather";
  tool.description = "Get the weather for a city";
  tool.parameters_json_schema = R"({"type":"object","properties":{}})";

  ChatMessage user_message;
  user_message.role = "user";
  user_message.content = "weather?";

  EXPECT_THROW(provider.Chat({user_message}, {tool}), std::runtime_error);
  EXPECT_EQ(mock_server.request_count(), 0);
}
