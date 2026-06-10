#pragma once

#include "kestral/core/document.hpp"
#include "kestral/core/document_batch_consumer.hpp"
#include "kestral/search/tokenizer.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kestral {

struct SearchOptions {
  std::size_t top_k = 10;
  double k1 = 1.2;
  double b = 0.75;
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

struct PostingList {
  std::vector<std::uint32_t> doc_indexes;
  std::vector<std::uint16_t> term_frequencies;
};

class InvertedIndexSegment;

class LexicalSegmentBuilder final : public DocumentBatchConsumer {
public:
  LexicalSegmentBuilder();

  void consume(std::span<const Document> documents) override;
  [[nodiscard]] std::size_t pending_document_count() const;
  [[nodiscard]] InvertedIndexSegment build() &&;

private:
  void index_document(const Document &document);
  void add_tokens(const std::vector<std::string> &tokens,
                  std::uint16_t weight,
                  std::unordered_map<std::string, std::uint16_t> &term_frequencies,
                  std::uint32_t &weighted_length) const;

  static constexpr std::uint16_t kTitleWeight = 2;
  static constexpr std::uint16_t kBodyWeight = 1;

  Tokenizer tokenizer_;
  std::vector<IndexedLexicalDocument> documents_;
  std::unordered_map<std::string, PostingList> postings_;
  std::uint64_t total_document_length_ = 0;
};

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
      std::unordered_map<std::string, PostingList> postings,
      double average_document_length);

  [[nodiscard]] std::vector<SearchResult>
  search(std::span<const std::string> query_terms, SearchOptions options) const;

  Tokenizer tokenizer_;
  std::vector<IndexedLexicalDocument> documents_;
  std::unordered_map<std::string, PostingList> postings_;
  double average_document_length_ = 0.0;
};

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
  std::vector<std::shared_ptr<const InvertedIndexSegment>> segments_;
};

} // namespace kestral
