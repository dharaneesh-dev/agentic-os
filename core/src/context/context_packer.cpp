#include "cooper/core/context/context_packer.hpp"

namespace cooper::core::context {

int ContextPacker::EstimateTokens(const std::string& text) const {
  return static_cast<int>(text.size() / 4);
}

PackedContext ContextPacker::Pack(const std::vector<parser::CodeChunk>& ranked_chunks, int token_budget) const {
  PackedContext packed;
  packed.total_tokens_estimate = 0;

  for (const auto& chunk : ranked_chunks) {
    int chunk_tokens = EstimateTokens(chunk.source_text);
    if (packed.total_tokens_estimate + chunk_tokens > token_budget) {
      break;
    }
    packed.included_chunks.push_back(chunk);
    packed.total_tokens_estimate += chunk_tokens;
  }

  return packed;
}

}  // namespace cooper::core::context
