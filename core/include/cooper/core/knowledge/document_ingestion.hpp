#pragma once

#include "cooper/core/data/database.hpp"
#include "cooper/core/embeddings/embedding_provider.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cooper::core::knowledge {

struct DocumentSection {
  std::string heading;
  std::string content;
};

std::vector<DocumentSection> ChunkDocument(const std::filesystem::path& path);

// Persists documents to the database for durable, cross-run knowledge storage. Unrelated to
// SearchCodebaseTool, which re-derives its in-memory index from disk on every run.
class DocumentIngestor {
 public:
  DocumentIngestor(data::IDatabase& db, embeddings::EmbeddingProvider& embedder);

  int64_t Ingest(int64_t codebase_id, const std::filesystem::path& path);

 private:
  data::IDatabase& db_;
  embeddings::EmbeddingProvider& embedder_;
};

}  // namespace cooper::core::knowledge
