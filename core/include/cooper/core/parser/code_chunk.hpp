#pragma once

#include <string>

namespace cooper::core::parser {

struct CodeChunk {
  std::string kind;
  std::string name;
  int start_line;
  int end_line;
  std::string source_text;
};

}  // namespace cooper::core::parser
