#include "cooper/core/agent/read_file_tool.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <random>
#include <stdexcept>

using cooper::core::agent::ReadFileTool;

namespace {

std::filesystem::path MakeTempRepo() {
  std::random_device rd;
  std::filesystem::path dir =
      std::filesystem::temp_directory_path() / ("cooper_core_read_file_tool_test_" + std::to_string(rd()));
  std::filesystem::create_directories(dir);
  return dir;
}

void WriteFile(const std::filesystem::path& path, const std::string& content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  file << content;
}

}  // namespace

class ReadFileToolTest : public ::testing::Test {
 protected:
  void SetUp() override { repo_root_ = MakeTempRepo(); }
  void TearDown() override { std::filesystem::remove_all(repo_root_); }

  std::filesystem::path repo_root_;
};

TEST_F(ReadFileToolTest, DefinitionHasExpectedName) {
  ReadFileTool tool(repo_root_);
  auto definition = tool.Definition();
  EXPECT_EQ(definition.name, "read_file");
  EXPECT_FALSE(definition.parameters_json_schema.empty());
}

TEST_F(ReadFileToolTest, ReadsExistingFileContent) {
  WriteFile(repo_root_ / "foo.py", "print('hello')\n");
  ReadFileTool tool(repo_root_);

  EXPECT_EQ(tool.Execute(R"({"path":"foo.py"})"), "print('hello')\n");
}

TEST_F(ReadFileToolTest, ReadsFileInSubdirectory) {
  WriteFile(repo_root_ / "pkg" / "mod.py", "x = 1\n");
  ReadFileTool tool(repo_root_);

  EXPECT_EQ(tool.Execute(R"({"path":"pkg/mod.py"})"), "x = 1\n");
}

TEST_F(ReadFileToolTest, ThrowsOnMissingFile) {
  ReadFileTool tool(repo_root_);
  EXPECT_THROW(tool.Execute(R"({"path":"nope.py"})"), std::runtime_error);
}

TEST_F(ReadFileToolTest, ThrowsOnPathEscapeAttempt) {
  WriteFile(repo_root_.parent_path() / "secret.txt", "top secret");
  ReadFileTool tool(repo_root_);

  EXPECT_THROW(tool.Execute(R"({"path":"../secret.txt"})"), std::runtime_error);
}
