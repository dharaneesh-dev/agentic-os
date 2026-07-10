#pragma once

#include "cooper/core/embeddings/embedding_provider.hpp"

namespace cooper::core::embeddings {

class MockEmbeddingProvider : public EmbeddingProvider {
 public:
  explicit MockEmbeddingProvider(size_t dim = 64);

  std::vector<float> Embed(const std::string& text) override;
  size_t Dimension() const override;

 private:
  size_t dim_;
};

}  // namespace cooper::core::embeddings
