#include "cooper/core/llm/gemini_provider.hpp"

#include "llm_test_server.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atomic>

using cooper::core::llm::ChatMessage;
using cooper::core::llm::ChatResult;
using cooper::core::llm::GeminiProvider;
using cooper::core::llm::ProviderConfig;
using cooper::core::llm::ToolDefinition;
using cooper::core::llm::testing::MockServer;

TEST(GeminiProviderTest, PlainTextResponseNoTools) {
  MockServer mock_server;
  std::atomic<bool> request_shape_ok{false};

  mock_server.server().Post(
      "/v1beta/models/gemini-test:generateContent", [&](const httplib::Request& req, httplib::Response& res) {
        mock_server.RecordRequest();
        nlohmann::json body = nlohmann::json::parse(req.body);
        request_shape_ok = req.get_param_value("key") == "test-key" && !body.contains("tools") &&
                            body.at("contents").at(0).at("role") == "user" &&
                            body.at("contents").at(0).at("parts").at(0).at("text") == "hi";
        res.set_content(
            R"({"candidates":[{"content":{"role":"model","parts":[{"text":"Hello there"}]}}],)"
            R"("usageMetadata":{"promptTokenCount":5,"candidatesTokenCount":3}})",
            "application/json");
      });

  ProviderConfig config;
  config.provider_name = "gemini";
  config.base_url = mock_server.base_url();
  config.model = "gemini-test";
  config.api_key = "test-key";

  GeminiProvider provider(config);
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

TEST(GeminiProviderTest, ToolCallResponse) {
  MockServer mock_server;
  std::atomic<bool> request_shape_ok{false};

  mock_server.server().Post(
      "/v1beta/models/gemini-test:generateContent", [&](const httplib::Request& req, httplib::Response& res) {
        mock_server.RecordRequest();
        nlohmann::json body = nlohmann::json::parse(req.body);
        request_shape_ok =
            body.at("tools").at(0).at("functionDeclarations").at(0).at("name") == "get_weather";
        res.set_content(
            R"({"candidates":[{"content":{"role":"model","parts":[)"
            R"({"functionCall":{"name":"get_weather","args":{"city":"Tokyo"}}}]}}],)"
            R"("usageMetadata":{"promptTokenCount":10,"candidatesTokenCount":8}})",
            "application/json");
      });

  ProviderConfig config;
  config.provider_name = "gemini";
  config.base_url = mock_server.base_url();
  config.model = "gemini-test";
  config.api_key = "test-key";
  config.assume_tool_calling_supported = true;

  GeminiProvider provider(config);
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

TEST(GeminiProviderTest, ChatWithToolsThrowsWhenToolCallingNotAssumed) {
  MockServer mock_server;
  mock_server.server().Post(
      "/v1beta/models/gemini-test:generateContent", [&](const httplib::Request&, httplib::Response& res) {
        mock_server.RecordRequest();
        res.set_content("{}", "application/json");
      });

  ProviderConfig config;
  config.provider_name = "gemini";
  config.base_url = mock_server.base_url();
  config.model = "gemini-test";
  config.api_key = "test-key";
  config.assume_tool_calling_supported = false;

  GeminiProvider provider(config);
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
