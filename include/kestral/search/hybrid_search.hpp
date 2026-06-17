#pragma once

#include "kestral/search/lexical_index.hpp"
#include "kestral/search/vector_index.hpp"
#include <span>
#include <string_view>
#include <vector>
#include <cstdint>

namespace kestral {

struct HybridSearchResult {
  std::uint64_t document_id;
  float score; // Reciprocal Rank Fusion (RRF) score
};

class HybridSearchEngine {
public:
  HybridSearchEngine(const PublishedLexicalIndex &lexical_index,
                     const VectorIndex &vector_index);

  std::vector<HybridSearchResult> search(std::string_view text_query,
                                         std::span<const float> vector_query,
                                         std::size_t top_k = 10,
                                         float rrf_k = 60.0f) const;

private:
  const PublishedLexicalIndex &lexical_index_;
  const VectorIndex &vector_index_;
};

} // namespace kestral
