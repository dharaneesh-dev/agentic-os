#pragma once

#include <filesystem>
#include <vector>

#include "cooper/core/parser/code_chunk.hpp"

namespace cooper::core::parser {

class Parser {
 public:
  std::vector<CodeChunk> ParseFile(const std::filesystem::path& path);
};

}  // namespace cooper::core::parser
