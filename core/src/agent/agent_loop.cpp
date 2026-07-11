#include "cooper/core/agent/agent_loop.hpp"

#include <stdexcept>

namespace cooper::core::agent {

AgentLoop::AgentLoop(llm::LlmProvider& provider, std::vector<std::unique_ptr<Tool>> tools, AgentLoopConfig config)
    : provider_(provider), tools_(std::move(tools)), config_(std::move(config)) {}

Tool* AgentLoop::FindTool(const std::string& name) const {
  for (const auto& tool : tools_) {
    if (tool->Definition().name == name) {
      return tool.get();
    }
  }
  return nullptr;
}

AgentLoopResult AgentLoop::Run(const std::string& user_prompt) {
  std::vector<llm::ToolDefinition> tool_defs;
  for (const auto& tool : tools_) {
    tool_defs.push_back(tool->Definition());
  }
  tool_defs.push_back(config_.finish_tool);

  llm::ChatMessage system_message;
  system_message.role = "system";
  system_message.content = config_.system_prompt;

  llm::ChatMessage user_message;
  user_message.role = "user";
  user_message.content = user_prompt;

  std::vector<llm::ChatMessage> messages = {system_message, user_message};

  AgentLoopResult result;
  result.finished = false;
  result.total_steps = 0;

  for (int step = 0; step < config_.max_steps; ++step) {
    llm::ChatResult chat_result = provider_.Chat(messages, tool_defs);
    ++result.total_steps;

    if (!chat_result.has_tool_call) {
      result.last_text_response = chat_result.text;
      break;
    }

    llm::ChatMessage assistant_message;
    assistant_message.role = "assistant";
    assistant_message.content = chat_result.text;
    assistant_message.tool_calls = chat_result.tool_calls;
    messages.push_back(assistant_message);

    bool finished_this_step = false;
    for (const auto& call : chat_result.tool_calls) {
      if (call.tool_name == "finish") {
        result.finished = true;
        result.finish_arguments_json = call.arguments_json;
        finished_this_step = true;
        break;
      }

      Tool* tool = FindTool(call.tool_name);
      if (tool == nullptr) {
        throw std::runtime_error("AgentLoop: model requested unknown tool '" + call.tool_name + "'");
      }

      std::string tool_content;
      try {
        tool_content = tool->Execute(call.arguments_json);
      } catch (const std::exception& error) {
        tool_content = error.what();
      }

      llm::ChatMessage tool_message;
      tool_message.role = "tool";
      tool_message.tool_call_id = call.id;
      tool_message.tool_name = call.tool_name;
      tool_message.content = tool_content;
      messages.push_back(tool_message);
    }

    if (finished_this_step) {
      break;
    }
  }

  result.transcript = messages;
  return result;
}

}  // namespace cooper::core::agent
