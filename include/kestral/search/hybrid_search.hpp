#pragma once

#include "kestral/search/lexical_index.hpp"
#include "kestral/search/vector_index.hpp"
#include <span>
#include <string_view>
#include <vector>
#include <cstdint>

#include "kestral/search/query_cache.hpp"

namespace kestral {

struct HybridSearchResult {
  std::uint64_t document_id;
  float score; // Reciprocal Rank Fusion (RRF) score
};

class HybridSearchEngine {
public:
  HybridSearchEngine(const PublishedLexicalIndex &lexical_index,
                     const VectorIndex &vector_index,
                     std::shared_ptr<QueryCache> cache = nullptr,
                     std::shared_ptr<DeletedDocs> deleted_docs = nullptr);

  std::vector<HybridSearchResult> search(std::string_view text_query,
                                         std::span<const float> vector_query,
                                         std::size_t top_k = 10,
                                         float rrf_k = 60.0f) const;

private:
  const PublishedLexicalIndex &lexical_index_;
  const VectorIndex &vector_index_;
  std::shared_ptr<QueryCache> cache_;
  std::shared_ptr<DeletedDocs> deleted_docs_;
};

} // namespace kestral
