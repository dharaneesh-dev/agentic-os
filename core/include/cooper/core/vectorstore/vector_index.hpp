#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace hnswlib {
template <typename T>
class HierarchicalNSW;
class L2Space;
}  // namespace hnswlib

namespace cooper::core::vectorstore {

class VectorIndex {
 public:
  explicit VectorIndex(size_t dim, size_t max_elements);
  ~VectorIndex();

  VectorIndex(VectorIndex&&) noexcept;
  VectorIndex& operator=(VectorIndex&&) noexcept;
  VectorIndex(const VectorIndex&) = delete;
  VectorIndex& operator=(const VectorIndex&) = delete;

  void Add(size_t id, const std::vector<float>& vector);
  std::vector<std::pair<size_t, float>> Search(const std::vector<float>& query, size_t k) const;

 private:
  size_t dim_;
  std::unique_ptr<hnswlib::L2Space> space_;
  std::unique_ptr<hnswlib::HierarchicalNSW<float>> index_;
};

}  // namespace cooper::core::vectorstore
