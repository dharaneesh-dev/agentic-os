#include "cooper/core/agent/agent_loop.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using cooper::core::agent::AgentLoop;
using cooper::core::agent::AgentLoopConfig;
using cooper::core::agent::AgentLoopResult;
using cooper::core::agent::Tool;
using cooper::core::llm::ChatMessage;
using cooper::core::llm::ChatResult;
using cooper::core::llm::LlmProvider;
using cooper::core::llm::ToolCallRequest;
using cooper::core::llm::ToolDefinition;

namespace {

class FakeLlmProvider : public LlmProvider {
 public:
  explicit FakeLlmProvider(std::vector<ChatResult> scripted_responses)
      : scripted_responses_(std::move(scripted_responses)) {}

  ChatResult Chat(const std::vector<ChatMessage>& messages, const std::vector<ToolDefinition>& /*tools*/) override {
    seen_messages_.push_back(messages);
    if (call_index_ >= scripted_responses_.size()) {
      throw std::runtime_error("FakeLlmProvider: no more scripted responses");
    }
    return scripted_responses_[call_index_++];
  }

  bool SupportsToolCalling() const override { return true; }
  std::string Name() const override { return "fake"; }

  const std::vector<std::vector<ChatMessage>>& SeenMessages() const { return seen_messages_; }

 private:
  std::vector<ChatResult> scripted_responses_;
  size_t call_index_ = 0;
  std::vector<std::vector<ChatMessage>> seen_messages_;
};

class RecordingTool : public Tool {
 public:
  explicit RecordingTool(std::string name, std::string result) : name_(std::move(name)), result_(std::move(result)) {}

  ToolDefinition Definition() const override {
    ToolDefinition def;
    def.name = name_;
    def.description = "test tool";
    def.parameters_json_schema = R"({"type":"object","properties":{}})";
    return def;
  }

  std::string Execute(const std::string& arguments_json) override {
    ++call_count;
    last_arguments_json = arguments_json;
    return result_;
  }

  int call_count = 0;
  std::string last_arguments_json;

 private:
  std::string name_;
  std::string result_;
};

class ThrowingTool : public Tool {
 public:
  explicit ThrowingTool(std::string name, std::string error_message)
      : name_(std::move(name)), error_message_(std::move(error_message)) {}

  ToolDefinition Definition() const override {
    ToolDefinition def;
    def.name = name_;
    def.description = "test tool that always throws";
    def.parameters_json_schema = R"({"type":"object","properties":{}})";
    return def;
  }

  std::string Execute(const std::string& /*arguments_json*/) override { throw std::runtime_error(error_message_); }

 private:
  std::string name_;
  std::string error_message_;
};

ChatResult MakeToolCallResult(const std::string& tool_name, const std::string& arguments_json,
                               const std::string& call_id) {
  ChatResult result;
  result.has_tool_call = true;
  ToolCallRequest call;
  call.id = call_id;
  call.tool_name = tool_name;
  call.arguments_json = arguments_json;
  result.tool_calls = {call};
  return result;
}

ChatResult MakeTextResult(const std::string& text) {
  ChatResult result;
  result.has_tool_call = false;
  result.text = text;
  return result;
}

AgentLoopConfig MakeConfig(int max_steps) {
  AgentLoopConfig config;
  config.max_steps = max_steps;
  config.system_prompt = "test system prompt";

  ToolDefinition finish;
  finish.name = "finish";
  finish.description = "signals completion";
  finish.parameters_json_schema = R"({"type":"object","properties":{"explanation":{"type":"string"}}})";
  config.finish_tool = finish;
  return config;
}

}  // namespace

TEST(AgentLoopTest, MultiStepSearchReadWriteFinishScenario) {
  std::vector<ChatResult> scripted = {
      MakeToolCallResult("search_codebase", R"({"query":"truncate"})", "call-1"),
      MakeToolCallResult("read_file", R"({"path":"string_utils.py"})", "call-2"),
      MakeToolCallResult("write_file", R"({"path":"string_utils.py","content":"..."})", "call-3"),
      MakeToolCallResult("finish", R"({"explanation":"done"})", "call-4"),
  };
  FakeLlmProvider provider(std::move(scripted));

  auto search_tool = std::make_unique<RecordingTool>("search_codebase", "search result");
  auto read_tool = std::make_unique<RecordingTool>("read_file", "file content");
  auto write_tool = std::make_unique<RecordingTool>("write_file", "wrote 10 bytes");
  RecordingTool* search_tool_ptr = search_tool.get();
  RecordingTool* read_tool_ptr = read_tool.get();
  RecordingTool* write_tool_ptr = write_tool.get();

  std::vector<std::unique_ptr<Tool>> tools;
  tools.push_back(std::move(search_tool));
  tools.push_back(std::move(read_tool));
  tools.push_back(std::move(write_tool));

  AgentLoop loop(provider, std::move(tools), MakeConfig(15));
  AgentLoopResult result = loop.Run("add a truncate function");

  EXPECT_TRUE(result.finished);
  EXPECT_EQ(result.finish_arguments_json, R"({"explanation":"done"})");
  EXPECT_GT(result.total_steps, 1);
  EXPECT_EQ(result.total_steps, 4);

  EXPECT_EQ(search_tool_ptr->call_count, 1);
  EXPECT_EQ(read_tool_ptr->call_count, 1);
  EXPECT_EQ(write_tool_ptr->call_count, 1);
  EXPECT_EQ(search_tool_ptr->last_arguments_json, R"({"query":"truncate"})");

  ASSERT_EQ(result.transcript.size(), 9u);
  EXPECT_EQ(result.transcript[0].role, "system");
  EXPECT_EQ(result.transcript[1].role, "user");

  EXPECT_EQ(result.transcript[2].role, "assistant");
  EXPECT_EQ(result.transcript[3].role, "tool");
  EXPECT_EQ(result.transcript[3].tool_name, "search_codebase");
  EXPECT_EQ(result.transcript[3].content, "search result");

  EXPECT_EQ(result.transcript[4].role, "assistant");
  EXPECT_EQ(result.transcript[5].role, "tool");
  EXPECT_EQ(result.transcript[5].tool_name, "read_file");
  EXPECT_EQ(result.transcript[5].content, "file content");

  EXPECT_EQ(result.transcript[6].role, "assistant");
  EXPECT_EQ(result.transcript[7].role, "tool");
  EXPECT_EQ(result.transcript[7].tool_name, "write_file");
  EXPECT_EQ(result.transcript[7].content, "wrote 10 bytes");

  EXPECT_EQ(result.transcript[8].role, "assistant");
  ASSERT_EQ(result.transcript[8].tool_calls.size(), 1u);
  EXPECT_EQ(result.transcript[8].tool_calls[0].tool_name, "finish");
}

TEST(AgentLoopTest, StopsWithNotFinishedWhenBudgetExhausted) {
  std::vector<ChatResult> scripted;
  for (int i = 0; i < 5; ++i) {
    scripted.push_back(MakeToolCallResult("read_file", R"({"path":"x.py"})", "call-" + std::to_string(i)));
  }
  FakeLlmProvider provider(std::move(scripted));

  std::vector<std::unique_ptr<Tool>> tools;
  tools.push_back(std::make_unique<RecordingTool>("read_file", "content"));

  AgentLoop loop(provider, std::move(tools), MakeConfig(3));
  AgentLoopResult result = loop.Run("task");

  EXPECT_FALSE(result.finished);
  EXPECT_EQ(result.total_steps, 3);
}

TEST(AgentLoopTest, ToolExecutionFailureIsFedBackAsToolMessageAndLoopContinues) {
  std::vector<ChatResult> scripted = {
      MakeToolCallResult("read_file", R"({"path":"missing.py"})", "call-1"),
      MakeTextResult("ok, I see the error"),
  };
  FakeLlmProvider provider(std::move(scripted));

  std::vector<std::unique_ptr<Tool>> tools;
  tools.push_back(std::make_unique<ThrowingTool>("read_file", "read_file: no such file: missing.py"));

  AgentLoop loop(provider, std::move(tools), MakeConfig(15));
  AgentLoopResult result = loop.Run("task");

  EXPECT_FALSE(result.finished);
  EXPECT_EQ(result.total_steps, 2);
  EXPECT_EQ(result.last_text_response, "ok, I see the error");

  ASSERT_EQ(provider.SeenMessages().size(), 2u);
  const std::vector<ChatMessage>& second_call_messages = provider.SeenMessages()[1];
  bool found_error_message = false;
  for (const auto& message : second_call_messages) {
    if (message.role == "tool" && message.content.find("no such file") != std::string::npos) {
      found_error_message = true;
    }
  }
  EXPECT_TRUE(found_error_message);
}

TEST(AgentLoopTest, ThrowsOnUnknownToolName) {
  std::vector<ChatResult> scripted = {
      MakeToolCallResult("does_not_exist", "{}", "call-1"),
  };
  FakeLlmProvider provider(std::move(scripted));

  std::vector<std::unique_ptr<Tool>> tools;
  tools.push_back(std::make_unique<RecordingTool>("read_file", "content"));

  AgentLoop loop(provider, std::move(tools), MakeConfig(15));

  EXPECT_THROW(loop.Run("task"), std::runtime_error);
}
