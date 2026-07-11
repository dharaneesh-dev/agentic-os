#include "cooper/core/agent/search_codebase_tool.hpp"

#include "cooper/core/embeddings/mock_embedding_provider.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <random>

using cooper::core::agent::SearchCodebaseTool;
using cooper::core::embeddings::MockEmbeddingProvider;

namespace {

std::filesystem::path MakeTempRepo() {
  std::random_device rd;
  std::filesystem::path dir =
      std::filesystem::temp_directory_path() / ("cooper_core_search_codebase_tool_test_" + std::to_string(rd()));
  std::filesystem::create_directories(dir);
  return dir;
}

void WriteFile(const std::filesystem::path& path, const std::string& content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  file << content;
}

}  // namespace

class SearchCodebaseToolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    repo_root_ = MakeTempRepo();
    WriteFile(repo_root_ / "math_utils.py", "def add(a, b):\n    return a + b\n");
    WriteFile(repo_root_ / "string_utils.py", "def slugify(text):\n    return text.lower()\n");
    WriteFile(repo_root_ / ".git" / "ignored.py", "def should_be_skipped():\n    pass\n");
  }

  void TearDown() override { std::filesystem::remove_all(repo_root_); }

  std::filesystem::path repo_root_;
};

TEST_F(SearchCodebaseToolTest, DefinitionHasExpectedName) {
  MockEmbeddingProvider embedder(16);
  SearchCodebaseTool tool(repo_root_, embedder, 1000);
  EXPECT_EQ(tool.Definition().name, "search_codebase");
}

TEST_F(SearchCodebaseToolTest, ReturnsPackedChunksWithFileHeader) {
  MockEmbeddingProvider embedder(16);
  SearchCodebaseTool tool(repo_root_, embedder, 1000);

  std::string result = tool.Execute(R"({"query":"add two numbers"})");

  EXPECT_NE(result.find("### math_utils.py"), std::string::npos);
  EXPECT_NE(result.find("### string_utils.py"), std::string::npos);
}

TEST_F(SearchCodebaseToolTest, SkipsGitDirectory) {
  MockEmbeddingProvider embedder(16);
  SearchCodebaseTool tool(repo_root_, embedder, 100000);

  std::string result = tool.Execute(R"({"query":"anything"})");

  EXPECT_EQ(result.find("should_be_skipped"), std::string::npos);
}

TEST_F(SearchCodebaseToolTest, RespectsTokenBudget) {
  MockEmbeddingProvider embedder(16);
  SearchCodebaseTool tool(repo_root_, embedder, 1);

  EXPECT_TRUE(tool.Execute(R"({"query":"anything"})").empty());
}

TEST_F(SearchCodebaseToolTest, EmptyRepoReturnsEmptyString) {
  std::filesystem::path empty_repo = MakeTempRepo();
  MockEmbeddingProvider embedder(16);
  SearchCodebaseTool tool(empty_repo, embedder, 1000);

  EXPECT_TRUE(tool.Execute(R"({"query":"anything"})").empty());
  std::filesystem::remove_all(empty_repo);
}
