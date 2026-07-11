#include "cooper/core/agent/write_file_tool.hpp"

#include "path_utils.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace cooper::core::agent {

WriteFileTool::WriteFileTool(std::filesystem::path repo_root) : repo_root_(std::move(repo_root)) {}

llm::ToolDefinition WriteFileTool::Definition() const {
  llm::ToolDefinition tool;
  tool.name = "write_file";
  tool.description = "Writes the given content to a file at a path relative to the repository root, "
                      "creating parent directories and overwriting any existing file.";
  tool.parameters_json_schema =
      R"({"type":"object","properties":{"path":{"type":"string"},"content":{"type":"string"}},)"
      R"("required":["path","content"]})";
  return tool;
}

std::string WriteFileTool::Execute(const std::string& arguments_json) {
  nlohmann::json arguments = nlohmann::json::parse(arguments_json);
  std::string relative_path = arguments.at("path").get<std::string>();
  std::string content = arguments.at("content").get<std::string>();

  std::filesystem::path resolved = detail::ResolveInRepo(repo_root_, relative_path);
  std::filesystem::create_directories(resolved.parent_path());

  std::ofstream file(resolved, std::ios::binary | std::ios::trunc);
  if (!file) {
    throw std::runtime_error("write_file: could not open file for writing: " + relative_path);
  }
  file << content;
  file.close();

  return "wrote " + std::to_string(content.size()) + " bytes to " + relative_path;
}

}  // namespace cooper::core::agent
