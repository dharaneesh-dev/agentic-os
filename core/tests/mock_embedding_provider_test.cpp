#include "cooper/core/embeddings/mock_embedding_provider.hpp"

#include <gtest/gtest.h>

using cooper::core::embeddings::MockEmbeddingProvider;

TEST(MockEmbeddingProviderTest, SameInputProducesIdenticalVector) {
  MockEmbeddingProvider provider(16);

  auto first = provider.Embed("hello world");
  auto second = provider.Embed("hello world");

  EXPECT_EQ(first, second);
}

TEST(MockEmbeddingProviderTest, DifferentInputsProduceDifferentVectors) {
  MockEmbeddingProvider provider(16);

  auto a = provider.Embed("hello world");
  auto b = provider.Embed("goodbye world");

  EXPECT_NE(a, b);
}

TEST(MockEmbeddingProviderTest, DimensionMatchesVectorSize) {
  MockEmbeddingProvider provider(32);

  auto embedding = provider.Embed("some text");

  EXPECT_EQ(provider.Dimension(), 32u);
  EXPECT_EQ(embedding.size(), 32u);
}
