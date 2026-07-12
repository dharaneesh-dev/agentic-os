#pragma once

#include "cooper/core/parser/code_chunk.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace cooper::core::knowledge {

struct DependencyEdge {
  std::string from_chunk_key;
  std::string to_chunk_key;
  std::string edge_type;  // "calls" or "imports"
};

// Name-based heuristic only: edges come from matching identifiers against other ingested
// chunks' names, not from real symbol resolution, so name collisions across unrelated
// files/functions can produce edges that aren't true dependencies.
class DependencyGraph {
 public:
  void AddChunk(const std::string& chunk_key, const std::string& file_path, const parser::CodeChunk& chunk);
  void Build();

  std::vector<std::string> DependenciesOf(const std::string& chunk_key) const;
  std::vector<std::string> DependentsOf(const std::string& chunk_key) const;

 private:
  struct Entry {
    std::string chunk_key;
    std::string file_path;
    parser::CodeChunk chunk;
  };

  std::vector<Entry> entries_;
  std::vector<DependencyEdge> edges_;
  std::unordered_map<std::string, std::vector<std::string>> dependencies_;
  std::unordered_map<std::string, std::vector<std::string>> dependents_;
};

}  // namespace cooper::core::knowledge
