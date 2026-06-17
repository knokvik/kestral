#include "kestral/search/hybrid_search.hpp"

#include <algorithm>
#include <unordered_map>

namespace kestral {

HybridSearchEngine::HybridSearchEngine(const PublishedLexicalIndex &lexical_index,
                                       const VectorIndex &vector_index,
                                       std::shared_ptr<QueryCache> cache)
    : lexical_index_(lexical_index), vector_index_(vector_index), cache_(std::move(cache)) {}

std::vector<HybridSearchResult>
HybridSearchEngine::search(std::string_view text_query,
                           std::span<const float> vector_query,
                           std::size_t top_k, float rrf_k) const {
  // 1. Perform individual searches (we fetch top_k * 2 from each to ensure overlap if possible)
  std::vector<SearchResult> lexical_results;
  
  if (cache_) {
    CacheKey key{std::string(text_query), top_k * 2, false};
    auto cached = cache_->get(key);
    if (cached) {
      lexical_results = *std::move(cached);
    } else {
      lexical_results = lexical_index_.search(text_query, {.top_k = top_k * 2, .require_all_terms = false});
      cache_->put(key, lexical_results);
    }
  } else {
    lexical_results = lexical_index_.search(text_query, {.top_k = top_k * 2, .require_all_terms = false});
  }

  auto vector_results = vector_index_.search(vector_query, top_k * 2);

  // 2. Accumulate RRF scores
  std::unordered_map<std::uint64_t, float> rrf_scores;

  for (std::size_t i = 0; i < lexical_results.size(); ++i) {
    const float rank = static_cast<float>(i + 1);
    rrf_scores[lexical_results[i].document_id] += 1.0f / (rrf_k + rank);
  }

  for (std::size_t i = 0; i < vector_results.size(); ++i) {
    const float rank = static_cast<float>(i + 1);
    rrf_scores[vector_results[i].document_id] += 1.0f / (rrf_k + rank);
  }

  // 3. Convert map to vector and sort
  std::vector<HybridSearchResult> combined_results;
  combined_results.reserve(rrf_scores.size());
  for (const auto &[doc_id, score] : rrf_scores) {
    combined_results.push_back({doc_id, score});
  }

  std::sort(combined_results.begin(), combined_results.end(),
            [](const HybridSearchResult &a, const HybridSearchResult &b) {
              return a.score > b.score; // Descending order
            });

  // 4. Truncate to top_k
  if (combined_results.size() > top_k) {
    combined_results.resize(top_k);
  }

  return combined_results;
}

} // namespace kestral
