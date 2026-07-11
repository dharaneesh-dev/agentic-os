#pragma once

#include "cooper/core/agent/tool.hpp"
#include "cooper/core/embeddings/embedding_provider.hpp"
#include "cooper/core/parser/code_chunk.hpp"
#include "cooper/core/vectorstore/vector_index.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace cooper::core::agent {

// Builds its index lazily on the first Execute() call rather than at construction, so
// constructing this tool for a role that never ends up calling it stays cheap.
class SearchCodebaseTool : public Tool {
 public:
  SearchCodebaseTool(std::filesystem::path repo_root, embeddings::EmbeddingProvider& embedder, int token_budget);

  llm::ToolDefinition Definition() const override;
  std::string Execute(const std::string& arguments_json) override;

 private:
  struct IndexedChunk {
    std::string file_path;
    parser::CodeChunk chunk;
  };

  void EnsureIndexBuilt();

  std::filesystem::path repo_root_;
  embeddings::EmbeddingProvider& embedder_;
  int token_budget_;
  bool index_built_ = false;
  std::vector<IndexedChunk> chunks_by_id_;
  std::unique_ptr<vectorstore::VectorIndex> index_;
};

}  // namespace cooper::core::agent
