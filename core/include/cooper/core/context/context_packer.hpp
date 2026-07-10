#pragma once

#include <string>
#include <vector>

#include "cooper/core/parser/code_chunk.hpp"

namespace cooper::core::context {

struct PackedContext {
  std::vector<parser::CodeChunk> included_chunks;
  int total_tokens_estimate;
};

class ContextPacker {
 public:
  PackedContext Pack(const std::vector<parser::CodeChunk>& ranked_chunks, int token_budget) const;

 private:
  int EstimateTokens(const std::string& text) const;
};

}  // namespace cooper::core::context
