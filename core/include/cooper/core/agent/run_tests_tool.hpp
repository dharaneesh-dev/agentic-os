#pragma once

#include "cooper/core/agent/tool.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace cooper::core::agent {

class RunTestsTool : public Tool {
 public:
  RunTestsTool(std::filesystem::path repo_root, std::vector<std::string> test_command, std::string docker_image,
               int timeout_seconds, std::filesystem::path orchestrator_dir,
               std::string python_executable = "python3");

  llm::ToolDefinition Definition() const override;
  std::string Execute(const std::string& arguments_json) override;

 private:
  std::filesystem::path repo_root_;
  std::vector<std::string> test_command_;
  std::string docker_image_;
  int timeout_seconds_;
  std::filesystem::path orchestrator_dir_;
  std::string python_executable_;
};

}  // namespace cooper::core::agent
