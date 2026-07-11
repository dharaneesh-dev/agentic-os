#include "cooper/core/llm/anthropic_provider.hpp"

#include "llm_test_server.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atomic>

using cooper::core::llm::AnthropicProvider;
using cooper::core::llm::ChatMessage;
using cooper::core::llm::ChatResult;
using cooper::core::llm::ProviderConfig;
using cooper::core::llm::ToolDefinition;
using cooper::core::llm::testing::MockServer;

TEST(AnthropicProviderTest, PlainTextResponseNoTools) {
  MockServer mock_server;
  std::atomic<bool> request_shape_ok{false};

  mock_server.server().Post("/v1/messages", [&](const httplib::Request& req, httplib::Response& res) {
    mock_server.RecordRequest();
    nlohmann::json body = nlohmann::json::parse(req.body);
    request_shape_ok = req.get_header_value("x-api-key") == "test-key" &&
                        req.get_header_value("anthropic-version") == "2023-06-01" &&
                        body.at("model") == "claude-test" && !body.contains("tools") &&
                        body.at("messages").at(0).at("role") == "user";
    res.set_content(
        R"({"content":[{"type":"text","text":"Hello there"}],)"
        R"("usage":{"input_tokens":5,"output_tokens":3}})",
        "application/json");
  });

  ProviderConfig config;
  config.provider_name = "anthropic";
  config.base_url = mock_server.base_url();
  config.model = "claude-test";
  config.api_key = "test-key";

  AnthropicProvider provider(config);
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

TEST(AnthropicProviderTest, ToolCallResponse) {
  MockServer mock_server;
  std::atomic<bool> request_shape_ok{false};

  mock_server.server().Post("/v1/messages", [&](const httplib::Request& req, httplib::Response& res) {
    mock_server.RecordRequest();
    nlohmann::json body = nlohmann::json::parse(req.body);
    request_shape_ok = body.at("tools").at(0).at("name") == "get_weather" &&
                        body.at("tools").at(0).contains("input_schema");
    res.set_content(
        R"({"content":[{"type":"tool_use","id":"toolu_1","name":"get_weather",)"
        R"("input":{"city":"Tokyo"}}],"usage":{"input_tokens":10,"output_tokens":8}})",
        "application/json");
  });

  ProviderConfig config;
  config.provider_name = "anthropic";
  config.base_url = mock_server.base_url();
  config.model = "claude-test";
  config.api_key = "test-key";
  config.assume_tool_calling_supported = true;

  AnthropicProvider provider(config);
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
  EXPECT_EQ(result.tool_calls[0].id, "toolu_1");
  EXPECT_EQ(result.tool_calls[0].tool_name, "get_weather");
  nlohmann::json arguments = nlohmann::json::parse(result.tool_calls[0].arguments_json);
  EXPECT_EQ(arguments.at("city"), "Tokyo");
}

TEST(AnthropicProviderTest, TwoTurnToolRoundTripMergesToolResultIntoUserTurn) {
  MockServer mock_server;
  std::atomic<int> call_index{0};
  std::atomic<bool> second_request_reconstructed_history{false};

  mock_server.server().Post("/v1/messages", [&](const httplib::Request& req, httplib::Response& res) {
    mock_server.RecordRequest();
    nlohmann::json body = nlohmann::json::parse(req.body);
    int index = call_index++;
    if (index == 0) {
      res.set_content(
          R"({"content":[{"type":"tool_use","id":"toolu_1","name":"get_weather",)"
          R"("input":{"city":"Tokyo"}}],"usage":{"input_tokens":10,"output_tokens":8}})",
          "application/json");
    } else {
      const nlohmann::json& messages = body.at("messages");
      second_request_reconstructed_history =
          messages.size() == 3 && messages[0].at("role") == "user" &&
          messages[1].at("role") == "assistant" &&
          messages[1].at("content").at(0).at("type") == "tool_use" &&
          messages[1].at("content").at(0).at("id") == "toolu_1" &&
          messages[2].at("role") == "user" &&
          messages[2].at("content").at(0).at("type") == "tool_result" &&
          messages[2].at("content").at(0).at("tool_use_id") == "toolu_1" &&
          messages[2].at("content").at(0).at("content") == "18 degrees celsius";
      res.set_content(
          R"({"content":[{"type":"text","text":"It is 18 degrees in Tokyo."}],)"
          R"("usage":{"input_tokens":20,"output_tokens":10}})",
          "application/json");
    }
  });

  ProviderConfig config;
  config.provider_name = "anthropic";
  config.base_url = mock_server.base_url();
  config.model = "claude-test";
  config.api_key = "test-key";
  config.assume_tool_calling_supported = true;

  AnthropicProvider provider(config);
  ToolDefinition tool;
  tool.name = "get_weather";
  tool.description = "Get the weather for a city";
  tool.parameters_json_schema =
      R"({"type":"object","properties":{"city":{"type":"string"}},"required":["city"]})";

  ChatMessage user_message;
  user_message.role = "user";
  user_message.content = "weather in tokyo?";

  ChatResult first_result = provider.Chat({user_message}, {tool});
  ASSERT_TRUE(first_result.has_tool_call);
  ASSERT_EQ(first_result.tool_calls.size(), 1u);

  ChatMessage assistant_message;
  assistant_message.role = "assistant";
  assistant_message.tool_calls = first_result.tool_calls;

  ChatMessage tool_result_message;
  tool_result_message.role = "tool";
  tool_result_message.tool_call_id = first_result.tool_calls[0].id;
  tool_result_message.tool_name = first_result.tool_calls[0].tool_name;
  tool_result_message.content = "18 degrees celsius";

  ChatResult second_result = provider.Chat({user_message, assistant_message, tool_result_message}, {tool});

  EXPECT_TRUE(second_request_reconstructed_history.load());
  EXPECT_EQ(mock_server.request_count(), 2);
  EXPECT_FALSE(second_result.has_tool_call);
  EXPECT_EQ(second_result.text, "It is 18 degrees in Tokyo.");
}

TEST(AnthropicProviderTest, ChatWithToolsThrowsWhenToolCallingNotAssumed) {
  MockServer mock_server;
  mock_server.server().Post("/v1/messages", [&](const httplib::Request&, httplib::Response& res) {
    mock_server.RecordRequest();
    res.set_content("{}", "application/json");
  });

  ProviderConfig config;
  config.provider_name = "anthropic";
  config.base_url = mock_server.base_url();
  config.model = "claude-test";
  config.api_key = "test-key";
  config.assume_tool_calling_supported = false;

  AnthropicProvider provider(config);
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

TEST(AnthropicProviderTest, EmbedThrows) {
  ProviderConfig config;
  config.provider_name = "anthropic";
  config.base_url = "http://127.0.0.1:1";
  config.model = "claude-test";
  config.api_key = "test-key";

  AnthropicProvider provider(config);
  EXPECT_THROW(provider.Embed("hello"), std::runtime_error);
  EXPECT_THROW(provider.Dimension(), std::runtime_error);
}
