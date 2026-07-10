#include "cooper/core/vectorstore/vector_index.hpp"

#include <gtest/gtest.h>

using cooper::core::vectorstore::VectorIndex;

TEST(VectorIndexTest, FindsNearestNeighborById) {
  VectorIndex index(3, 10);

  index.Add(0, {1.0F, 0.0F, 0.0F});
  index.Add(1, {0.0F, 1.0F, 0.0F});
  index.Add(2, {0.0F, 0.0F, 1.0F});
  index.Add(3, {0.9F, 0.1F, 0.0F});

  auto results = index.Search({1.0F, 0.0F, 0.0F}, 1);

  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results.front().first, 0u);
}

TEST(VectorIndexTest, ReturnsRequestedNumberOfNeighborsOrderedByDistance) {
  VectorIndex index(3, 10);

  index.Add(10, {1.0F, 0.0F, 0.0F});
  index.Add(11, {0.9F, 0.1F, 0.0F});
  index.Add(12, {0.0F, 1.0F, 0.0F});
  index.Add(13, {0.0F, 0.0F, 1.0F});

  auto results = index.Search({1.0F, 0.0F, 0.0F}, 2);

  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(results[0].first, 10u);
  EXPECT_EQ(results[1].first, 11u);
  EXPECT_LE(results[0].second, results[1].second);
}
