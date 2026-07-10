#pragma once

#include "cooper/core/embeddings/embedding_provider.hpp"

namespace cooper::core::embeddings {

// Pending llama.cpp integration; every method throws until that dependency is wired up.
class LlamaEmbeddingProvider : public EmbeddingProvider {
 public:
  explicit LlamaEmbeddingProvider(const std::string& model_path);

  std::vector<float> Embed(const std::string& text) override;
  size_t Dimension() const override;

 private:
  std::string model_path_;
};

}  // namespace cooper::core::embeddings
