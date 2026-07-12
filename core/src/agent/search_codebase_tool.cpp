#include "cooper/core/agent/search_codebase_tool.hpp"

#include "cooper/core/context/context_packer.hpp"
#include "cooper/core/knowledge/document_ingestion.hpp"
#include "cooper/core/parser/parser.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>

namespace cooper::core::agent {

namespace {

bool IsCodeExtension(const std::string& extension) {
  static const std::set<std::string> kCodeExtensions = {".py", ".cpp", ".hpp", ".cc", ".h", ".cxx", ".hh"};
  return kCodeExtensions.count(extension) != 0;
}

bool IsDocExtension(const std::string& extension) { return extension == ".md" || extension == ".txt"; }

}  // namespace

SearchCodebaseTool::SearchCodebaseTool(std::filesystem::path repo_root, embeddings::EmbeddingProvider& embedder,
                                        int token_budget, bool enable_dependency_graph)
    : repo_root_(std::move(repo_root)),
      embedder_(embedder),
      token_budget_(token_budget),
      enable_dependency_graph_(enable_dependency_graph) {}

llm::ToolDefinition SearchCodebaseTool::Definition() const {
  llm::ToolDefinition tool;
  tool.name = "search_codebase";
  tool.description =
      "Semantically searches the repository's source code and documentation for content relevant to a query.";
  tool.parameters_json_schema = R"({"type":"object","properties":{"query":{"type":"string"}},"required":["query"]})";
  return tool;
}

std::string SearchCodebaseTool::ChunkKeyFor(const IndexedChunk& indexed) const {
  return indexed.file_path + "::" + indexed.chunk.name;
}

void SearchCodebaseTool::EnsureIndexBuilt() {
  if (index_built_) {
    return;
  }
  index_built_ = true;

  parser::Parser parser;
  std::vector<IndexedChunk> collected;

  for (auto it = std::filesystem::recursive_directory_iterator(repo_root_);
       it != std::filesystem::recursive_directory_iterator(); ++it) {
    if (it->is_directory() && it->path().filename() == ".git") {
      it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file()) {
      continue;
    }

    std::string extension = it->path().extension().string();
    std::string relative_path = std::filesystem::relative(it->path(), repo_root_).string();

    if (IsCodeExtension(extension)) {
      for (auto& chunk : parser.ParseFile(it->path())) {
        collected.push_back(IndexedChunk{relative_path, std::move(chunk)});
      }
    } else if (IsDocExtension(extension)) {
      for (auto& section : knowledge::ChunkDocument(it->path())) {
        parser::CodeChunk chunk;
        chunk.kind = "document";
        chunk.name = section.heading;
        chunk.start_line = 0;
        chunk.end_line = 0;
        chunk.source_text = std::move(section.content);
        collected.push_back(IndexedChunk{relative_path, std::move(chunk)});
      }
    }
  }

  index_ = std::make_unique<vectorstore::VectorIndex>(embedder_.Dimension(), std::max<size_t>(collected.size(), 1));
  for (size_t id = 0; id < collected.size(); ++id) {
    index_->Add(id, embedder_.Embed(collected[id].chunk.source_text));
    id_by_chunk_key_[ChunkKeyFor(collected[id])] = id;
    if (enable_dependency_graph_ && collected[id].chunk.kind != "document") {
      graph_.AddChunk(ChunkKeyFor(collected[id]), collected[id].file_path, collected[id].chunk);
    }
  }
  if (enable_dependency_graph_) {
    graph_.Build();
  }
  chunks_by_id_ = std::move(collected);
}

std::string SearchCodebaseTool::Execute(const std::string& arguments_json) {
  EnsureIndexBuilt();

  nlohmann::json arguments = nlohmann::json::parse(arguments_json);
  std::string query = arguments.at("query").get<std::string>();

  if (chunks_by_id_.empty()) {
    return "";
  }

  std::vector<float> query_vector = embedder_.Embed(query);
  size_t k = std::min<size_t>(5, chunks_by_id_.size());
  std::vector<std::pair<size_t, float>> matches = index_->Search(query_vector, k);

  std::vector<parser::CodeChunk> ranked_chunks;
  std::vector<std::string> ranked_paths;
  std::set<size_t> included_ids;
  for (const auto& [id, distance] : matches) {
    ranked_chunks.push_back(chunks_by_id_[id].chunk);
    ranked_paths.push_back(chunks_by_id_[id].file_path);
    included_ids.insert(id);
  }

  if (enable_dependency_graph_ && !matches.empty()) {
    size_t top_id = matches.front().first;
    std::string top_key = ChunkKeyFor(chunks_by_id_[top_id]);

    std::vector<std::string> related_keys = graph_.DependenciesOf(top_key);
    std::vector<std::string> dependents = graph_.DependentsOf(top_key);
    related_keys.insert(related_keys.end(), dependents.begin(), dependents.end());

    for (const auto& related_key : related_keys) {
      auto found = id_by_chunk_key_.find(related_key);
      if (found == id_by_chunk_key_.end() || included_ids.count(found->second) != 0) {
        continue;
      }
      included_ids.insert(found->second);
      ranked_chunks.push_back(chunks_by_id_[found->second].chunk);
      ranked_paths.push_back(chunks_by_id_[found->second].file_path);
    }
  }

  context::ContextPacker packer;
  context::PackedContext packed = packer.Pack(ranked_chunks, token_budget_);

  // ranked_chunks/ranked_paths share indices with packed.included_chunks: Pack() only truncates
  // a prefix's suffix on budget overrun, it never reorders.
  std::string output;
  for (size_t i = 0; i < packed.included_chunks.size(); ++i) {
    output += "### " + ranked_paths[i] + "\n" + packed.included_chunks[i].source_text + "\n\n";
  }
  return output;
}

}  // namespace cooper::core::agent
