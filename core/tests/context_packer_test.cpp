#include "cooper/core/context/context_packer.hpp"

#include <gtest/gtest.h>

using cooper::core::context::ContextPacker;
using cooper::core::context::PackedContext;
using cooper::core::parser::CodeChunk;

namespace {

CodeChunk MakeChunk(const std::string& name, size_t text_length) {
  CodeChunk chunk;
  chunk.kind = "function";
  chunk.name = name;
  chunk.start_line = 1;
  chunk.end_line = 1;
  chunk.source_text = std::string(text_length, 'x');
  return chunk;
}

}  // namespace

TEST(ContextPackerTest, GreedilyPacksUntilBudgetExhausted) {
  std::vector<CodeChunk> chunks = {
      MakeChunk("a", 40),
      MakeChunk("b", 40),
      MakeChunk("c", 40),
  };

  ContextPacker packer;
  PackedContext packed = packer.Pack(chunks, 20);

  EXPECT_EQ(packed.included_chunks.size(), 2u);
  EXPECT_LE(packed.total_tokens_estimate, 20);
  EXPECT_EQ(packed.included_chunks[0].name, "a");
  EXPECT_EQ(packed.included_chunks[1].name, "b");
}

TEST(ContextPackerTest, NeverExceedsBudget) {
  std::vector<CodeChunk> chunks = {
      MakeChunk("a", 400),
      MakeChunk("b", 4),
      MakeChunk("c", 4),
  };

  ContextPacker packer;
  PackedContext packed = packer.Pack(chunks, 5);

  EXPECT_TRUE(packed.included_chunks.empty());
  EXPECT_LE(packed.total_tokens_estimate, 5);
}

TEST(ContextPackerTest, IncludesAllWhenBudgetIsGenerous) {
  std::vector<CodeChunk> chunks = {
      MakeChunk("a", 40),
      MakeChunk("b", 40),
  };

  ContextPacker packer;
  PackedContext packed = packer.Pack(chunks, 1000);

  EXPECT_EQ(packed.included_chunks.size(), 2u);
  EXPECT_LE(packed.total_tokens_estimate, 1000);
}
