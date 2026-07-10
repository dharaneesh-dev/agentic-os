#include "cooper/core/parser/parser.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <tree_sitter/api.h>

extern "C" const TSLanguage* tree_sitter_python(void);

namespace cooper::core::parser {

namespace {

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Parser: unable to read file " + path.string());
  }
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

std::string NodeText(TSNode node, const std::string& source) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  return source.substr(start, end - start);
}

void CollectChunks(TSNode node, const std::string& source, std::vector<CodeChunk>& chunks) {
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; ++i) {
    TSNode child = ts_node_child(node, i);
    std::string type = ts_node_type(child);
    if (type == "function_definition" || type == "class_definition") {
      TSNode name_node = ts_node_child_by_field_name(child, "name", 4);
      std::string name = ts_node_is_null(name_node) ? std::string{} : NodeText(name_node, source);
      CodeChunk chunk;
      chunk.kind = type == "function_definition" ? "function" : "class";
      chunk.name = std::move(name);
      chunk.start_line = static_cast<int>(ts_node_start_point(child).row) + 1;
      chunk.end_line = static_cast<int>(ts_node_end_point(child).row) + 1;
      chunk.source_text = NodeText(child, source);
      chunks.push_back(std::move(chunk));
    }
    CollectChunks(child, source, chunks);
  }
}

}  // namespace

std::vector<CodeChunk> Parser::ParseFile(const std::filesystem::path& path) {
  std::string source = ReadFile(path);

  TSParser* ts_parser = ts_parser_new();
  if (ts_parser == nullptr) {
    throw std::runtime_error("Parser: failed to create tree-sitter parser");
  }
  ts_parser_set_language(ts_parser, tree_sitter_python());

  TSTree* tree = ts_parser_parse_string(ts_parser, nullptr, source.c_str(), static_cast<uint32_t>(source.size()));
  if (tree == nullptr) {
    ts_parser_delete(ts_parser);
    throw std::runtime_error("Parser: failed to parse " + path.string());
  }

  TSNode root = ts_tree_root_node(tree);
  std::vector<CodeChunk> chunks;
  CollectChunks(root, source, chunks);

  ts_tree_delete(tree);
  ts_parser_delete(ts_parser);
  return chunks;
}

}  // namespace cooper::core::parser
