#include "kestral/search/vector_index.hpp"

#include <stdexcept>

namespace kestral {

VectorIndex::VectorIndex(std::size_t dimensions)
    : dimensions_(dimensions) {
  auto result = unum::usearch::index_dense_t::make(
      unum::usearch::metric_punned_t(dimensions, unum::usearch::metric_kind_t::cos_k));
  if (!result) {
    throw std::runtime_error(std::string("Failed to initialize USearch index: ") + result.error.release());
  }
  index_ = std::move(result.index);
}

void VectorIndex::consume(std::span<const Document> documents) {
  if (index_.size() + documents.size() > index_.capacity()) {
    index_.reserve(index_.capacity() + documents.size() + 10000);
  }
  for (const auto &document : documents) {
    if (document.embedding.size() != dimensions_) {
      throw std::invalid_argument("Document embedding dimension mismatch");
    }
    index_.add(document.id, document.embedding.data());
  }
}

std::vector<VectorSearchResult> VectorIndex::search(
    std::span<const float> query_vector, std::size_t top_k) const {
  if (query_vector.size() != dimensions_) {
      throw std::invalid_argument("Query embedding dimension mismatch");
  }

  auto results = index_.search(query_vector.data(), top_k);
  std::vector<VectorSearchResult> search_results;
  search_results.reserve(results.size());
  for (std::size_t i = 0; i < results.size(); ++i) {
    search_results.push_back({
        .document_id = static_cast<std::uint64_t>(results[i].member.key),
        .distance = static_cast<float>(results[i].distance),
    });
  }
  return search_results;
}

std::size_t VectorIndex::size() const {
  return index_.size();
}

std::size_t VectorIndex::dimensions() const {
  return dimensions_;
}

} // namespace kestral
