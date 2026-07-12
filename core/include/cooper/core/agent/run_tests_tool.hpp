#pragma once

#include "cooper/core/agent/tool.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace cooper::core::agent {

struct RunTestsResult {
  bool passed = false;
  std::string test_stdout;
  std::string test_stderr;
  int exit_code = 0;
};

class RunTestsTool : public Tool {
 public:
  RunTestsTool(std::filesystem::path repo_root, std::vector<std::string> test_command, std::string docker_image,
               int timeout_seconds, std::filesystem::path orchestrator_dir,
               std::string python_executable = "python3");

  llm::ToolDefinition Definition() const override;
  std::string Execute(const std::string& arguments_json) override;

  // runs the sandboxed test command once and returns the structured result, without formatting
  // it into the model-facing string Execute() produces. The authoritative gate for callers (like
  // the orchestrator) that need to inspect passed/stdout/stderr/exit_code directly.
  RunTestsResult RunOnce();

 private:
  std::filesystem::path repo_root_;
  std::vector<std::string> test_command_;
  std::string docker_image_;
  int timeout_seconds_;
  std::filesystem::path orchestrator_dir_;
  std::string python_executable_;
};

}  // namespace cooper::core::agent
