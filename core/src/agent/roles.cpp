#include "cooper/core/agent/roles.hpp"

#include "cooper/core/agent/read_file_tool.hpp"
#include "cooper/core/agent/run_tests_tool.hpp"
#include "cooper/core/agent/search_codebase_tool.hpp"
#include "cooper/core/agent/write_file_tool.hpp"

namespace cooper::core::agent {

namespace {

llm::ToolDefinition FinishTool(const std::string& schema) {
  llm::ToolDefinition tool;
  tool.name = "finish";
  tool.description = "Signals that the task is complete and reports the outcome.";
  tool.parameters_json_schema = schema;
  return tool;
}

}  // namespace

RoleSetup MakeCoderRole(const std::filesystem::path& repo_root, embeddings::EmbeddingProvider& embedder,
                         int search_token_budget, const RunTestsConfig& run_tests_config) {
  RoleSetup setup;
  setup.tools.push_back(std::make_unique<ReadFileTool>(repo_root));
  setup.tools.push_back(std::make_unique<SearchCodebaseTool>(repo_root, embedder, search_token_budget));
  setup.tools.push_back(std::make_unique<WriteFileTool>(repo_root));
  setup.tools.push_back(std::make_unique<RunTestsTool>(repo_root, run_tests_config.test_command,
                                                        run_tests_config.docker_image,
                                                        run_tests_config.timeout_seconds,
                                                        run_tests_config.orchestrator_dir,
                                                        run_tests_config.python_executable));

  setup.config.max_steps = 15;
  setup.config.system_prompt =
      "You are the Coder agent in Cooper, an automated coding pipeline. Use read_file and "
      "search_codebase to understand the repository, write_file to make the necessary change, "
      "and run_tests to confirm it works. Call finish once you are done.";
  setup.config.finish_tool =
      FinishTool(R"({"type":"object","properties":{"explanation":{"type":"string"}},"required":["explanation"]})");
  return setup;
}

RoleSetup MakeProductManagerRole(const std::filesystem::path& repo_root, embeddings::EmbeddingProvider& embedder,
                                  int search_token_budget) {
  RoleSetup setup;
  setup.tools.push_back(std::make_unique<ReadFileTool>(repo_root));
  setup.tools.push_back(std::make_unique<SearchCodebaseTool>(repo_root, embedder, search_token_budget));

  setup.config.max_steps = 5;
  setup.config.system_prompt =
      "You are the Product Manager agent in Cooper. Use read_file and search_codebase to "
      "investigate the repository, then break the business requirement down into a technical "
      "spec of subtasks. Call finish with the summary and subtasks once ready.";
  setup.config.finish_tool = FinishTool(
      R"({"type":"object","properties":{"summary":{"type":"string"},"subtasks":{"type":"array",)"
      R"("items":{"type":"object","properties":{"id":{"type":"string"},"description":{"type":"string"},)"
      R"("target_files":{"type":"array","items":{"type":"string"}}},)"
      R"("required":["id","description","target_files"]}}},"required":["summary","subtasks"]})");
  return setup;
}

RoleSetup MakeManagerRole(const std::filesystem::path& repo_root) {
  RoleSetup setup;
  setup.tools.push_back(std::make_unique<ReadFileTool>(repo_root));

  setup.config.max_steps = 5;
  setup.config.system_prompt =
      "You are the Manager agent in Cooper. Use read_file to look at the current state of the "
      "repository and decide whether the change is acceptable. Call finish with your approval "
      "decision and feedback.";
  setup.config.finish_tool = FinishTool(
      R"({"type":"object","properties":{"approved":{"type":"boolean"},"feedback":{"type":"string"}},)"
      R"("required":["approved","feedback"]})");
  return setup;
}

}  // namespace cooper::core::agent
