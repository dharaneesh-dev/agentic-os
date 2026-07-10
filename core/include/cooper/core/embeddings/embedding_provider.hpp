#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cooper::core::embeddings {

class EmbeddingProvider {
 public:
  virtual ~EmbeddingProvider() = default;
  virtual std::vector<float> Embed(const std::string& text) = 0;
  virtual size_t Dimension() const = 0;
};

}  // namespace cooper::core::embeddings
