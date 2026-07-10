#include "cooper/core/embeddings/mock_embedding_provider.hpp"

#include <random>

namespace cooper::core::embeddings {

MockEmbeddingProvider::MockEmbeddingProvider(size_t dim) : dim_(dim) {}

std::vector<float> MockEmbeddingProvider::Embed(const std::string& text) {
  std::mt19937_64 engine(std::hash<std::string>{}(text));
  std::uniform_real_distribution<float> distribution(-1.0F, 1.0F);

  std::vector<float> embedding(dim_);
  for (float& component : embedding) {
    component = distribution(engine);
  }
  return embedding;
}

size_t MockEmbeddingProvider::Dimension() const { return dim_; }

}  // namespace cooper::core::embeddings
