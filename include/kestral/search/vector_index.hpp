#pragma once

#include "kestral/core/document.hpp"
#include "kestral/core/document_batch_consumer.hpp"

#include <usearch/index_dense.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kestral {

struct VectorSearchResult {
  std::uint64_t document_id = 0;
  float distance = 0.0f;
};

class VectorIndex final : public DocumentBatchConsumer {
public:
  explicit VectorIndex(std::size_t dimensions);

  void consume(std::span<const Document> documents) override;

  [[nodiscard]] std::vector<VectorSearchResult> search(
      std::span<const float> query_vector, std::size_t top_k) const;

  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] std::size_t dimensions() const;

private:
  std::size_t dimensions_;
  unum::usearch::index_dense_t index_;
};

} // namespace kestral
