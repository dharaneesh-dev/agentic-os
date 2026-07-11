#include "cooper/core/agent/read_file_tool.hpp"

#include "path_utils.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace cooper::core::agent {

ReadFileTool::ReadFileTool(std::filesystem::path repo_root) : repo_root_(std::move(repo_root)) {}

llm::ToolDefinition ReadFileTool::Definition() const {
  llm::ToolDefinition tool;
  tool.name = "read_file";
  tool.description = "Reads the full contents of a file at a path relative to the repository root.";
  tool.parameters_json_schema = R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})";
  return tool;
}

std::string ReadFileTool::Execute(const std::string& arguments_json) {
  nlohmann::json arguments = nlohmann::json::parse(arguments_json);
  std::string relative_path = arguments.at("path").get<std::string>();

  std::filesystem::path resolved = detail::ResolveInRepo(repo_root_, relative_path);
  if (!std::filesystem::is_regular_file(resolved)) {
    throw std::runtime_error("read_file: no such file: " + relative_path);
  }

  std::ifstream file(resolved, std::ios::binary);
  if (!file) {
    throw std::runtime_error("read_file: could not open file: " + relative_path);
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

}  // namespace cooper::core::agent
