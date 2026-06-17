#pragma once

#include "kestral/core/document.hpp"
#include "kestral/core/document_batch_consumer.hpp"
#include "kestral/search/deleted_docs.hpp"
#include "kestral/search/tokenizer.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kestral {

// ---------------------------------------------------------------------------
// Transparent hash & equality — enables heterogeneous lookup on
// unordered_map<string, V> with string_view keys, avoiding temporary
// string construction on every map lookup.
// ---------------------------------------------------------------------------
struct TransparentStringHash {
  using is_transparent = void;
  std::size_t operator()(std::string_view sv) const noexcept {
    return std::hash<std::string_view>{}(sv);
  }
};

struct TransparentStringEqual {
  using is_transparent = void;
  bool operator()(std::string_view a, std::string_view b) const noexcept {
    return a == b;
  }
};

// Convenience alias for maps that accept heterogeneous (string_view) lookups.
template <typename V>
using TransparentStringMap =
    std::unordered_map<std::string, V, TransparentStringHash,
                       TransparentStringEqual>;

// ---------------------------------------------------------------------------
// Search types
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

struct SearchOptions {
  std::size_t top_k = 10;
  double k1 = 1.2;
  double b = 0.75;
  bool require_all_terms = false;
  std::shared_ptr<const DeletedDocs> deleted_docs = nullptr;
};

struct SearchResult {
  std::uint64_t document_id = 0;
  double score = 0.0;
  std::uint32_t matched_terms = 0;
};

struct IndexedLexicalDocument {
  std::uint64_t id = 0;
  std::uint32_t weighted_length = 0;
};

struct BuilderPostingList {
  std::vector<std::uint32_t> doc_indexes;
  std::vector<std::uint16_t> term_frequencies;
};

// ---------------------------------------------------------------------------
// Lexical segment builder (ingestion time)
// ---------------------------------------------------------------------------
class InvertedIndexSegment;

class LexicalSegmentBuilder final : public DocumentBatchConsumer {
public:
  LexicalSegmentBuilder();

  void consume(std::span<const Document> documents) override;
  [[nodiscard]] std::size_t pending_document_count() const;
  [[nodiscard]] InvertedIndexSegment build() &&;

private:
  void index_document(const Document &document);
  void add_tokens(std::span<const std::string_view> tokens,
                  std::uint16_t weight,
                  std::unordered_map<std::string_view, std::uint16_t>
                      &term_frequencies,
                  std::uint32_t &weighted_length) const;

  static constexpr std::uint16_t kTitleWeight = 2;
  static constexpr std::uint16_t kBodyWeight = 1;

  Tokenizer tokenizer_;
  std::vector<IndexedLexicalDocument> documents_;
  TransparentStringMap<BuilderPostingList> postings_;
  std::uint64_t total_document_length_ = 0;

  // Reusable scratch buffers — cleared per document, never deallocated.
  std::string scratch_title_;
  std::string scratch_body_;
  std::vector<std::string_view> title_views_;
  std::vector<std::string_view> body_views_;
  std::unordered_map<std::string_view, std::uint16_t> term_freq_scratch_;
};

struct TermMetadata {
  std::size_t postings_offset;
  std::size_t skips_offset;
  std::uint32_t document_frequency;
};

// ---------------------------------------------------------------------------
// Immutable inverted-index segment (query time)
// ---------------------------------------------------------------------------
class InvertedIndexSegment {
public:
  [[nodiscard]] std::size_t document_count() const;
  [[nodiscard]] std::size_t unique_term_count() const;
  [[nodiscard]] double average_document_length() const;

private:
  friend class LexicalSegmentBuilder;
  friend class PublishedLexicalIndex;

  InvertedIndexSegment(
      Tokenizer tokenizer,
      std::vector<IndexedLexicalDocument> documents,
      TransparentStringMap<TermMetadata> term_dict,
      std::vector<std::uint8_t> postings_buffer,
      std::vector<std::uint8_t> skips_buffer,
      double average_document_length);

  [[nodiscard]] std::vector<SearchResult>
  search(std::span<const std::string_view> query_terms,
         SearchOptions options) const;

  Tokenizer tokenizer_;
  std::vector<IndexedLexicalDocument> documents_;
  TransparentStringMap<TermMetadata> term_dict_;
  std::vector<std::uint8_t> postings_buffer_;
  std::vector<std::uint8_t> skips_buffer_;
  double average_document_length_ = 0.0;
};

// ---------------------------------------------------------------------------
// Published multi-segment index (read-only, thread-safe via shared_ptr)
// ---------------------------------------------------------------------------
class PublishedLexicalIndex {
public:
  void publish_segment(InvertedIndexSegment segment);

  [[nodiscard]] std::vector<SearchResult>
  search(std::string_view query, SearchOptions options = {}) const;

  [[nodiscard]] std::size_t segment_count() const;
  [[nodiscard]] std::size_t total_document_count() const;
  [[nodiscard]] std::size_t total_unique_term_count() const;

private:
  Tokenizer tokenizer_;
  mutable std::shared_mutex mutex_;
  std::vector<std::shared_ptr<const InvertedIndexSegment>> segments_;
};

} // namespace kestral
