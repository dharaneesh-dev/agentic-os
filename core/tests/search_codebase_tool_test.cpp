#include "cooper/core/agent/search_codebase_tool.hpp"

#include "cooper/core/embeddings/mock_embedding_provider.hpp"
#include "cooper/core/parser/parser.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

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

namespace {

std::filesystem::path MakeDependencyGraphFixtureRepo() {
  std::filesystem::path repo = MakeTempRepo();

  // checkout's body names calculate_total explicitly, giving the "calls" heuristic a real edge
  // to find; calculate_total itself carries no descriptive naming, so it should score poorly on
  // vector similarity alone.
  WriteFile(repo / "checkout.py",
            "def checkout(cart):\n"
            "    total_price = calculate_total(cart.items)\n"
            "    process_payment(total_price)\n"
            "    return total_price\n");
  WriteFile(repo / "orders.py", "def calculate_total(items):\n    return sum(items)\n");

  // Noise, unrelated to checkout/calculate_total: enough distinct chunks that calculate_total's
  // essentially-random (hash-seeded) vector distance has little chance of landing in the
  // vector-only top-K ahead of it, so it would not otherwise surface without the graph.
  for (int file_index = 0; file_index < 8; ++file_index) {
    std::string content;
    for (int fn_index = 0; fn_index < 6; ++fn_index) {
      content += "def noise_fn_" + std::to_string(file_index) + "_" + std::to_string(fn_index) + "(x):\n" +
                 "    return x + " + std::to_string(file_index * 6 + fn_index) + "\n\n";
    }
    WriteFile(repo / ("noise_" + std::to_string(file_index) + ".py"), content);
  }

  return repo;
}

}  // namespace

TEST_F(SearchCodebaseToolTest, DependencyGraphBoostsRelatedChunkIntoResults) {
  std::filesystem::path repo = MakeDependencyGraphFixtureRepo();

  // Querying with the exact text tree-sitter extracts for checkout's chunk (not a hand-typed
  // literal -- the extracted node's byte range excludes the trailing newline a literal would
  // include) guarantees an identical (distance-0) embedding under MockEmbeddingProvider's
  // hash-of-exact-text scheme, so checkout is deterministically the top vector hit regardless of
  // the noise chunks' random vectors.
  cooper::core::parser::Parser code_parser;
  std::string query = code_parser.ParseFile(repo / "checkout.py").front().source_text;
  std::string arguments = nlohmann::json{{"query", query}}.dump();

  MockEmbeddingProvider vector_only_embedder(16);
  SearchCodebaseTool vector_only_tool(repo, vector_only_embedder, 100000, /*enable_dependency_graph=*/false);
  std::string vector_only_result = vector_only_tool.Execute(arguments);

  MockEmbeddingProvider boosted_embedder(16);
  SearchCodebaseTool boosted_tool(repo, boosted_embedder, 100000, /*enable_dependency_graph=*/true);
  std::string boosted_result = boosted_tool.Execute(arguments);

  // Check for the "### orders.py" header, not a bare "calculate_total" substring: checkout's own
  // body calls calculate_total, so that substring is present in checkout's chunk regardless of
  // whether orders.py's chunk was actually included as a separate result.
  EXPECT_NE(vector_only_result.find("### checkout.py"), std::string::npos);
  EXPECT_EQ(vector_only_result.find("### orders.py"), std::string::npos);

  EXPECT_NE(boosted_result.find("### checkout.py"), std::string::npos);
  EXPECT_NE(boosted_result.find("### orders.py"), std::string::npos);

  EXPECT_NE(vector_only_result, boosted_result);

  std::filesystem::remove_all(repo);
}

TEST_F(SearchCodebaseToolTest, NoGraphBenefitMatchesVectorOnlyBehaviorExactly) {
  std::filesystem::path repo = MakeTempRepo();
  WriteFile(repo / "math_utils.py", "def add(a, b):\n    return a + b\n");
  WriteFile(repo / "string_utils.py", "def slugify(text):\n    return text.lower()\n");

  std::string arguments = R"({"query":"add two numbers"})";

  MockEmbeddingProvider vector_only_embedder(16);
  SearchCodebaseTool vector_only_tool(repo, vector_only_embedder, 1000, /*enable_dependency_graph=*/false);
  std::string vector_only_result = vector_only_tool.Execute(arguments);

  MockEmbeddingProvider boosted_embedder(16);
  SearchCodebaseTool boosted_tool(repo, boosted_embedder, 1000, /*enable_dependency_graph=*/true);
  std::string boosted_result = boosted_tool.Execute(arguments);

  EXPECT_EQ(vector_only_result, boosted_result);

  std::filesystem::remove_all(repo);
}
