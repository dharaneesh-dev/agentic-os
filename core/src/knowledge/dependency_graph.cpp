#include "cooper/core/knowledge/dependency_graph.hpp"

#include <filesystem>
#include <regex>
#include <set>
#include <tuple>

namespace cooper::core::knowledge {

namespace {

std::string ModuleName(const std::string& file_path) { return std::filesystem::path(file_path).stem().string(); }

bool IsPython(const std::string& file_path) { return std::filesystem::path(file_path).extension() == ".py"; }

}  // namespace

void DependencyGraph::AddChunk(const std::string& chunk_key, const std::string& file_path,
                                const parser::CodeChunk& chunk) {
  entries_.push_back(Entry{chunk_key, file_path, chunk});
}

void DependencyGraph::Build() {
  edges_.clear();
  dependencies_.clear();
  dependents_.clear();

  std::set<std::tuple<std::string, std::string, std::string>> seen_edges;
  auto AddEdge = [&](const std::string& from, const std::string& to, const std::string& type) {
    if (from == to || !seen_edges.insert({from, to, type}).second) {
      return;
    }
    edges_.push_back(DependencyEdge{from, to, type});
    dependencies_[from].push_back(to);
    dependents_[to].push_back(from);
  };

  // Only a chunk's own extracted source_text is scanned, never the raw file: a module-level
  // "import x" that sits outside any function/class body is invisible to this heuristic.
  static const std::regex kImportRegex(R"(\bimport\s+([A-Za-z_][A-Za-z0-9_]*))");
  static const std::regex kFromImportRegex(R"(\bfrom\s+([A-Za-z_][A-Za-z0-9_]*)\s+import\b)");

  for (const auto& entry : entries_) {
    if (!IsPython(entry.file_path)) {
      continue;
    }
    std::set<std::string> imported_modules;
    for (auto it = std::sregex_iterator(entry.chunk.source_text.begin(), entry.chunk.source_text.end(),
                                         kImportRegex);
         it != std::sregex_iterator(); ++it) {
      imported_modules.insert((*it)[1].str());
    }
    for (auto it = std::sregex_iterator(entry.chunk.source_text.begin(), entry.chunk.source_text.end(),
                                         kFromImportRegex);
         it != std::sregex_iterator(); ++it) {
      imported_modules.insert((*it)[1].str());
    }
    if (imported_modules.empty()) {
      continue;
    }
    for (const auto& other : entries_) {
      if (other.file_path != entry.file_path && imported_modules.count(ModuleName(other.file_path)) != 0) {
        AddEdge(entry.chunk_key, other.chunk_key, "imports");
      }
    }
  }

  // Bare "name(" substring matching: two unrelated chunks that happen to share a common function
  // name (e.g. "run", "process") will produce a spurious "calls" edge.
  static const std::regex kCallRegex(R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*\()");

  for (const auto& entry : entries_) {
    if (entry.chunk.kind != "function") {
      continue;
    }
    for (auto it = std::sregex_iterator(entry.chunk.source_text.begin(), entry.chunk.source_text.end(), kCallRegex);
         it != std::sregex_iterator(); ++it) {
      std::string called_name = (*it)[1].str();
      for (const auto& other : entries_) {
        if (other.chunk_key != entry.chunk_key && other.chunk.name == called_name) {
          AddEdge(entry.chunk_key, other.chunk_key, "calls");
        }
      }
    }
  }
}

std::vector<std::string> DependencyGraph::DependenciesOf(const std::string& chunk_key) const {
  auto it = dependencies_.find(chunk_key);
  return it == dependencies_.end() ? std::vector<std::string>{} : it->second;
}

std::vector<std::string> DependencyGraph::DependentsOf(const std::string& chunk_key) const {
  auto it = dependents_.find(chunk_key);
  return it == dependents_.end() ? std::vector<std::string>{} : it->second;
}

}  // namespace cooper::core::knowledge
