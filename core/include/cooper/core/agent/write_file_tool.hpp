#pragma once

#include "cooper/core/agent/tool.hpp"

#include <filesystem>

namespace cooper::core::agent {

class WriteFileTool : public Tool {
 public:
  explicit WriteFileTool(std::filesystem::path repo_root);

  llm::ToolDefinition Definition() const override;
  std::string Execute(const std::string& arguments_json) override;

 private:
  std::filesystem::path repo_root_;
};

}  // namespace cooper::core::agent
