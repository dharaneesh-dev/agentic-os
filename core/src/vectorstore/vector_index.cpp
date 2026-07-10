#include "cooper/core/vectorstore/vector_index.hpp"

#include <hnswlib/hnswlib.h>

#include <algorithm>
#include <stdexcept>

namespace cooper::core::vectorstore {

VectorIndex::VectorIndex(size_t dim, size_t max_elements)
    : dim_(dim),
      space_(std::make_unique<hnswlib::L2Space>(dim)),
      index_(std::make_unique<hnswlib::HierarchicalNSW<float>>(space_.get(), max_elements)) {}

VectorIndex::~VectorIndex() = default;
VectorIndex::VectorIndex(VectorIndex&&) noexcept = default;
VectorIndex& VectorIndex::operator=(VectorIndex&&) noexcept = default;

void VectorIndex::Add(size_t id, const std::vector<float>& vector) {
  if (vector.size() != dim_) {
    throw std::runtime_error("VectorIndex::Add: vector dimension mismatch");
  }
  index_->addPoint(vector.data(), id);
}

std::vector<std::pair<size_t, float>> VectorIndex::Search(const std::vector<float>& query, size_t k) const {
  if (query.size() != dim_) {
    throw std::runtime_error("VectorIndex::Search: query dimension mismatch");
  }
  auto result_queue = index_->searchKnn(query.data(), k);

  std::vector<std::pair<size_t, float>> results;
  results.reserve(result_queue.size());
  while (!result_queue.empty()) {
    auto [distance, label] = result_queue.top();
    results.emplace_back(label, distance);
    result_queue.pop();
  }
  std::reverse(results.begin(), results.end());
  return results;
}

}  // namespace cooper::core::vectorstore
