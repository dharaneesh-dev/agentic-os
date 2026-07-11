#pragma once

#include "cooper/core/agent/tool.hpp"
#include "cooper/core/llm/provider.hpp"

#include <memory>
#include <string>
#include <vector>

namespace cooper::core::agent {

struct AgentLoopConfig {
  int max_steps;
  std::string system_prompt;
  llm::ToolDefinition finish_tool;  // name must be "finish"; parameters_json_schema is caller-supplied
};

struct AgentLoopResult {
  bool finished;
  std::string finish_arguments_json;  // valid only when finished == true
  std::string last_text_response;     // diagnostic: set whenever the model responds with no tool call
  std::vector<llm::ChatMessage> transcript;
  int total_steps;
};

class AgentLoop {
 public:
  AgentLoop(llm::LlmProvider& provider, std::vector<std::unique_ptr<Tool>> tools, AgentLoopConfig config);

  AgentLoopResult Run(const std::string& user_prompt);

 private:
  Tool* FindTool(const std::string& name) const;

  llm::LlmProvider& provider_;
  std::vector<std::unique_ptr<Tool>> tools_;
  AgentLoopConfig config_;
};

}  // namespace cooper::core::agent
