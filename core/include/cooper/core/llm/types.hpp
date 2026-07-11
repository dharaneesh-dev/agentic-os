#pragma once

#include <string>
#include <vector>

namespace cooper::core::llm {

struct ToolDefinition {
  std::string name;
  std::string description;
  std::string parameters_json_schema; // raw JSON Schema text for the tool's parameters object
};

struct ToolCallRequest {
  std::string id; // provider-assigned; needed to correlate the result message back to this call
  std::string tool_name;
  std::string arguments_json; // JSON text of the arguments the model wants to invoke the tool with
};

struct ChatMessage {
  std::string role; // "system" | "user" | "assistant" | "tool"
  std::string content;
  std::vector<ToolCallRequest> tool_calls; // set on an assistant message that requested tool call(s)
  std::string tool_call_id; // set on a "tool" role message: which call this result answers
  std::string tool_name; // set on a "tool" role message
};

struct ChatResult {
  bool has_tool_call = false;
  std::vector<ToolCallRequest> tool_calls;
  std::string text;
  int prompt_tokens = 0;
  int completion_tokens = 0;
};

}  // namespace cooper::core::llm
