#include "cooper/core/agent/run_tests_tool.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <random>

using cooper::core::agent::RunTestsTool;

namespace {

std::filesystem::path MakeTempRepo() {
  std::random_device rd;
  std::filesystem::path dir =
      std::filesystem::temp_directory_path() / ("cooper_core_run_tests_tool_test_" + std::to_string(rd()));
  std::filesystem::create_directories(dir);
  return dir;
}

void WriteCheckScript(const std::filesystem::path& repo_root, const std::string& content) {
  std::ofstream file(repo_root / "check.py", std::ios::binary);
  file << content;
}

std::filesystem::path OrchestratorDir() { return std::filesystem::path(COOPER_ORCHESTRATOR_DIR); }

}  // namespace

class RunTestsToolTest : public ::testing::Test {
 protected:
  void TearDown() override {
    if (!repo_root_.empty()) {
      std::filesystem::remove_all(repo_root_);
    }
  }

  std::filesystem::path repo_root_;
};

TEST_F(RunTestsToolTest, DefinitionHasExpectedName) {
  RunTestsTool tool(std::filesystem::temp_directory_path(), {"python", "--version"}, "python:3.12-slim", 30,
                     OrchestratorDir());
  EXPECT_EQ(tool.Definition().name, "run_tests");
}

// Real end-to-end: this shells out to the real run_tests_cli.py, which spins up a real Docker
// container via the real, unmodified DockerSandbox. No mocking on either side of that boundary.
TEST_F(RunTestsToolTest, SurfacesPassingResultFromRealDockerSandbox) {
  repo_root_ = MakeTempRepo();
  WriteCheckScript(repo_root_, "print('all good')\n");

  RunTestsTool tool(repo_root_, {"python", "check.py"}, "python:3.12-slim", 60, OrchestratorDir());
  std::string result = tool.Execute("{}");

  EXPECT_NE(result.find("PASSED"), std::string::npos);
  EXPECT_NE(result.find("all good"), std::string::npos);
}

TEST_F(RunTestsToolTest, SurfacesFailingResultFromRealDockerSandbox) {
  repo_root_ = MakeTempRepo();
  WriteCheckScript(repo_root_, "import sys\nprint('boom', file=sys.stderr)\nsys.exit(1)\n");

  RunTestsTool tool(repo_root_, {"python", "check.py"}, "python:3.12-slim", 60, OrchestratorDir());
  std::string result = tool.Execute("{}");

  EXPECT_NE(result.find("FAILED"), std::string::npos);
  EXPECT_NE(result.find("boom"), std::string::npos);
}
