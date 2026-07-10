#include "cooper/core/parser/parser.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

using cooper::core::parser::CodeChunk;
using cooper::core::parser::Parser;

namespace {

std::filesystem::path SampleFixturePath() {
  return std::filesystem::path(COOPER_TEST_FIXTURES_DIR) / "sample.py";
}

const CodeChunk& FindChunk(const std::vector<CodeChunk>& chunks, const std::string& name) {
  auto it = std::find_if(chunks.begin(), chunks.end(), [&](const CodeChunk& c) { return c.name == name; });
  EXPECT_NE(it, chunks.end()) << "expected chunk named " << name;
  return *it;
}

}  // namespace

TEST(ParserTest, ParsesFunctionsAndClassFromFixture) {
  Parser parser;
  std::vector<CodeChunk> chunks = parser.ParseFile(SampleFixturePath());

  ASSERT_EQ(chunks.size(), 4u);

  int function_count = 0;
  int class_count = 0;
  for (const auto& chunk : chunks) {
    if (chunk.kind == "function") {
      ++function_count;
    } else if (chunk.kind == "class") {
      ++class_count;
    }
    EXPECT_LE(chunk.start_line, chunk.end_line);
  }
  EXPECT_EQ(function_count, 3);
  EXPECT_EQ(class_count, 1);
}

TEST(ParserTest, TopLevelDefsAreNonOverlapping) {
  Parser parser;
  std::vector<CodeChunk> chunks = parser.ParseFile(SampleFixturePath());

  const CodeChunk& add = FindChunk(chunks, "add");
  const CodeChunk& subtract = FindChunk(chunks, "subtract");
  const CodeChunk& calculator = FindChunk(chunks, "Calculator");

  EXPECT_EQ(add.kind, "function");
  EXPECT_EQ(subtract.kind, "function");
  EXPECT_EQ(calculator.kind, "class");

  EXPECT_LT(add.end_line, subtract.start_line);
  EXPECT_LT(subtract.end_line, calculator.start_line);
}

TEST(ParserTest, FindsNestedMethodInsideClass) {
  Parser parser;
  std::vector<CodeChunk> chunks = parser.ParseFile(SampleFixturePath());

  const CodeChunk& multiply = FindChunk(chunks, "multiply");
  const CodeChunk& calculator = FindChunk(chunks, "Calculator");

  EXPECT_EQ(multiply.kind, "function");
  EXPECT_GE(multiply.start_line, calculator.start_line);
  EXPECT_LE(multiply.end_line, calculator.end_line);
}

TEST(ParserTest, ThrowsOnUnreadableFile) {
  Parser parser;
  EXPECT_THROW(parser.ParseFile("/nonexistent/path/does_not_exist.py"), std::runtime_error);
}
