#include "cooper/core/agent/write_file_tool.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

using cooper::core::agent::WriteFileTool;

namespace {

std::filesystem::path MakeTempRepo() {
  std::random_device rd;
  std::filesystem::path dir =
      std::filesystem::temp_directory_path() / ("cooper_core_write_file_tool_test_" + std::to_string(rd()));
  std::filesystem::create_directories(dir);
  return dir;
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

}  // namespace

class WriteFileToolTest : public ::testing::Test {
 protected:
  void SetUp() override { repo_root_ = MakeTempRepo(); }
  void TearDown() override { std::filesystem::remove_all(repo_root_); }

  std::filesystem::path repo_root_;
};

TEST_F(WriteFileToolTest, DefinitionHasExpectedName) {
  WriteFileTool tool(repo_root_);
  auto definition = tool.Definition();
  EXPECT_EQ(definition.name, "write_file");
  EXPECT_FALSE(definition.parameters_json_schema.empty());
}

TEST_F(WriteFileToolTest, WritesNewFile) {
  WriteFileTool tool(repo_root_);

  std::string confirmation = tool.Execute(R"({"path":"foo.py","content":"print(1)\n"})");

  EXPECT_EQ(ReadFile(repo_root_ / "foo.py"), "print(1)\n");
  EXPECT_NE(confirmation.find("foo.py"), std::string::npos);
}

TEST_F(WriteFileToolTest, CreatesParentDirectories) {
  WriteFileTool tool(repo_root_);

  tool.Execute(R"({"path":"pkg/sub/mod.py","content":"x = 1\n"})");

  EXPECT_EQ(ReadFile(repo_root_ / "pkg" / "sub" / "mod.py"), "x = 1\n");
}

TEST_F(WriteFileToolTest, OverwritesExistingFile) {
  WriteFileTool tool(repo_root_);
  tool.Execute(R"({"path":"foo.py","content":"old\n"})");
  tool.Execute(R"({"path":"foo.py","content":"new\n"})");

  EXPECT_EQ(ReadFile(repo_root_ / "foo.py"), "new\n");
}

TEST_F(WriteFileToolTest, ThrowsOnPathEscapeAttempt) {
  WriteFileTool tool(repo_root_);
  EXPECT_THROW(tool.Execute(R"({"path":"../escape.py","content":"x"})"), std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(repo_root_.parent_path() / "escape.py"));
}
