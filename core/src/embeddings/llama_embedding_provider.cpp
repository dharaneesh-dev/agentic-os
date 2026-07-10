#include "cooper/core/embeddings/llama_embedding_provider.hpp"

#include <stdexcept>

namespace cooper::core::embeddings {

LlamaEmbeddingProvider::LlamaEmbeddingProvider(const std::string& model_path) : model_path_(model_path) {
  throw std::runtime_error("LlamaEmbeddingProvider not yet implemented — pending llama.cpp integration");
}

std::vector<float> LlamaEmbeddingProvider::Embed(const std::string&) {
  throw std::runtime_error("LlamaEmbeddingProvider not yet implemented — pending llama.cpp integration");
}

size_t LlamaEmbeddingProvider::Dimension() const {
  throw std::runtime_error("LlamaEmbeddingProvider not yet implemented — pending llama.cpp integration");
}

}  // namespace cooper::core::embeddings
