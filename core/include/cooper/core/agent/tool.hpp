#pragma once

#include "cooper/core/llm/types.hpp"

#include <string>

namespace cooper::core::agent {

class Tool {
 public:
  virtual ~Tool() = default;

  virtual llm::ToolDefinition Definition() const = 0;

  // Throws std::runtime_error on failure; AgentLoop catches it and feeds .what() back to the model.
  virtual std::string Execute(const std::string& arguments_json) = 0;
};

}  // namespace cooper::core::agent
