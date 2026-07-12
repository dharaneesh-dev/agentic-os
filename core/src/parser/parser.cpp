#include "cooper/core/parser/parser.hpp"

#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>

#include <tree_sitter/api.h>

extern "C" const TSLanguage* tree_sitter_python(void);
extern "C" const TSLanguage* tree_sitter_cpp(void);

namespace cooper::core::parser {

namespace {

enum class Language { kPython, kCpp };

std::optional<Language> LanguageForExtension(const std::string& extension) {
  if (extension == ".py") {
    return Language::kPython;
  }
  static const std::set<std::string> kCppExtensions = {".cpp", ".hpp", ".cc", ".h", ".cxx", ".hh"};
  if (kCppExtensions.count(extension) != 0) {
    return Language::kCpp;
  }
  return std::nullopt;
}

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

// A C++ function_definition's own "declarator" field is never the plain name: it's a chain of
// wrapper nodes (function_declarator, and for pointer/reference return types pointer_declarator/
// reference_declarator on top of that) down to the real name node (identifier, field_identifier
// for in-class methods, or qualified_identifier for out-of-line "Class::Method" definitions).
// Empirically, pointer_declarator exposes that wrapped node as a "declarator" field but
// reference_declarator does not, so the unnamed-children fallback below is required, not optional.
std::string ResolveDeclaratorName(TSNode node, const std::string& source) {
  TSNode declarator_field = ts_node_child_by_field_name(node, "declarator", 10);
  if (!ts_node_is_null(declarator_field)) {
    return ResolveDeclaratorName(declarator_field, source);
  }
  TSNode name_field = ts_node_child_by_field_name(node, "name", 4);
  if (!ts_node_is_null(name_field)) {
    return ResolveDeclaratorName(name_field, source);
  }
  uint32_t named_count = ts_node_named_child_count(node);
  for (uint32_t i = 0; i < named_count; ++i) {
    TSNode child = ts_node_named_child(node, i);
    std::string child_type = ts_node_type(child);
    if (child_type == "function_declarator" || child_type == "pointer_declarator" ||
        child_type == "reference_declarator" || child_type == "parenthesized_declarator" ||
        child_type == "array_declarator" || child_type == "qualified_identifier") {
      return ResolveDeclaratorName(child, source);
    }
  }
  return NodeText(node, source);
}

void CollectCppChunks(TSNode node, const std::string& source, std::vector<CodeChunk>& chunks) {
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; ++i) {
    TSNode child = ts_node_child(node, i);
    std::string type = ts_node_type(child);
    if (type == "function_definition") {
      CodeChunk chunk;
      chunk.kind = "function";
      chunk.name = ResolveDeclaratorName(child, source);
      chunk.start_line = static_cast<int>(ts_node_start_point(child).row) + 1;
      chunk.end_line = static_cast<int>(ts_node_end_point(child).row) + 1;
      chunk.source_text = NodeText(child, source);
      chunks.push_back(std::move(chunk));
    } else if (type == "class_specifier" || type == "struct_specifier") {
      // Forward declarations (e.g. "struct Foo;") share this node type but have no "body" field;
      // only definitions are chunks.
      TSNode body = ts_node_child_by_field_name(child, "body", 4);
      if (!ts_node_is_null(body)) {
        TSNode name_node = ts_node_child_by_field_name(child, "name", 4);
        CodeChunk chunk;
        chunk.kind = "class";
        chunk.name = ts_node_is_null(name_node) ? std::string{} : NodeText(name_node, source);
        chunk.start_line = static_cast<int>(ts_node_start_point(child).row) + 1;
        chunk.end_line = static_cast<int>(ts_node_end_point(child).row) + 1;
        chunk.source_text = NodeText(child, source);
        chunks.push_back(std::move(chunk));
      }
    }
    CollectCppChunks(child, source, chunks);
  }
}

}  // namespace

std::vector<CodeChunk> Parser::ParseFile(const std::filesystem::path& path) {
  std::optional<Language> language = LanguageForExtension(path.extension().string());
  if (!language) {
    throw std::runtime_error("Parser: unsupported file extension for " + path.string() +
                              "; only .py, .cpp, .hpp, .cc, .h, .cxx, .hh are currently supported");
  }

  std::string source = ReadFile(path);

  TSParser* ts_parser = ts_parser_new();
  if (ts_parser == nullptr) {
    throw std::runtime_error("Parser: failed to create tree-sitter parser");
  }
  ts_parser_set_language(ts_parser, *language == Language::kPython ? tree_sitter_python() : tree_sitter_cpp());

  TSTree* tree = ts_parser_parse_string(ts_parser, nullptr, source.c_str(), static_cast<uint32_t>(source.size()));
  if (tree == nullptr) {
    ts_parser_delete(ts_parser);
    throw std::runtime_error("Parser: failed to parse " + path.string());
  }

  TSNode root = ts_tree_root_node(tree);
  std::vector<CodeChunk> chunks;
  if (*language == Language::kPython) {
    CollectChunks(root, source, chunks);
  } else {
    CollectCppChunks(root, source, chunks);
  }

  ts_tree_delete(tree);
  ts_parser_delete(ts_parser);
  return chunks;
}

}  // namespace cooper::core::parser
