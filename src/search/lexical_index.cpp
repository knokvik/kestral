#include "kestral/search/lexical_index.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace {

struct AccumulatedScore {
  double score = 0.0;
  std::uint32_t matched_terms = 0;
};

double bm25_term_score(std::uint16_t term_frequency,
                       std::uint32_t document_length,
                       double average_document_length,
                       std::size_t document_count,
                       std::size_t document_frequency,
                       kestral::SearchOptions options) {
  if (document_count == 0 || document_frequency == 0 ||
      average_document_length <= 0.0) {
    return 0.0;
  }

  const double inverse_document_frequency =
      std::log1p((static_cast<double>(document_count) -
                  static_cast<double>(document_frequency) + 0.5) /
                 (static_cast<double>(document_frequency) + 0.5));
  const double normalized_length =
      static_cast<double>(document_length) / average_document_length;
  const double numerator =
      static_cast<double>(term_frequency) * (options.k1 + 1.0);
  const double denominator =
      static_cast<double>(term_frequency) +
      options.k1 * (1.0 - options.b + options.b * normalized_length);

  return inverse_document_frequency * (numerator / denominator);
}

} // namespace

namespace kestral {

LexicalSegmentBuilder::LexicalSegmentBuilder() = default;

void LexicalSegmentBuilder::consume(std::span<const Document> documents) {
  for (const auto &document : documents) {
    index_document(document);
  }
}

std::size_t LexicalSegmentBuilder::pending_document_count() const {
  return documents_.size();
}

InvertedIndexSegment LexicalSegmentBuilder::build() && {
  const double average_document_length =
      documents_.empty()
          ? 0.0
          : static_cast<double>(total_document_length_) /
                static_cast<double>(documents_.size());

  return InvertedIndexSegment(std::move(tokenizer_),
                              std::move(documents_),
                              std::move(postings_),
                              average_document_length);
}

void LexicalSegmentBuilder::index_document(const Document &document) {
  std::vector<std::string> title_tokens;
  std::vector<std::string> body_tokens;
  tokenizer_.tokenize_into(document.title, title_tokens);
  tokenizer_.tokenize_into(document.body, body_tokens);

  std::unordered_map<std::string, std::uint16_t> term_frequencies;
  term_frequencies.reserve(title_tokens.size() + body_tokens.size());

  std::uint32_t weighted_length = 0;
  add_tokens(title_tokens, kTitleWeight, term_frequencies, weighted_length);
  add_tokens(body_tokens, kBodyWeight, term_frequencies, weighted_length);

  const auto document_index = static_cast<std::uint32_t>(documents_.size());
  documents_.push_back({
      .id = document.id,
      .weighted_length = weighted_length,
  });
  total_document_length_ += weighted_length;

  for (auto &[term, frequency] : term_frequencies) {
    auto &posting_list = postings_[term];
    posting_list.doc_indexes.push_back(document_index);
    posting_list.term_frequencies.push_back(frequency);
  }
}

void LexicalSegmentBuilder::add_tokens(
    const std::vector<std::string> &tokens,
    std::uint16_t weight,
    std::unordered_map<std::string, std::uint16_t> &term_frequencies,
    std::uint32_t &weighted_length) const {
  weighted_length += static_cast<std::uint32_t>(tokens.size()) * weight;

  for (const auto &token : tokens) {
    term_frequencies[token] += weight;
  }
}

InvertedIndexSegment::InvertedIndexSegment(
    Tokenizer tokenizer,
    std::vector<IndexedLexicalDocument> documents,
    std::unordered_map<std::string, PostingList> postings,
    double average_document_length)
    : tokenizer_(std::move(tokenizer)),
      documents_(std::move(documents)),
      postings_(std::move(postings)),
      average_document_length_(average_document_length) {}

std::size_t InvertedIndexSegment::document_count() const {
  return documents_.size();
}

std::size_t InvertedIndexSegment::unique_term_count() const {
  return postings_.size();
}

double InvertedIndexSegment::average_document_length() const {
  return average_document_length_;
}

std::vector<SearchResult> InvertedIndexSegment::search(
    std::span<const std::string> query_terms,
    SearchOptions options) const {
  if (query_terms.empty() || documents_.empty()) {
    return {};
  }

  std::unordered_map<std::string, std::uint32_t> query_term_weights;
  query_term_weights.reserve(query_terms.size());
  for (const auto &term : query_terms) {
    query_term_weights[term] += 1;
  }

  std::unordered_map<std::uint32_t, AccumulatedScore> accumulators;

  for (const auto &[term, query_weight] : query_term_weights) {
    const auto posting_list_iterator = postings_.find(term);
    if (posting_list_iterator == postings_.end()) {
      continue;
    }

    const auto &posting_list = posting_list_iterator->second;
    const auto document_frequency = posting_list.doc_indexes.size();

    for (std::size_t index = 0; index < document_frequency; ++index) {
      const auto document_index = posting_list.doc_indexes[index];
      const auto term_frequency = posting_list.term_frequencies[index];
      const auto &document = documents_[document_index];

      auto &accumulated_score = accumulators[document_index];
      accumulated_score.score +=
          static_cast<double>(query_weight) *
          bm25_term_score(term_frequency,
                          document.weighted_length,
                          average_document_length_,
                          documents_.size(),
                          document_frequency,
                          options);
      accumulated_score.matched_terms += 1;
    }
  }

  std::vector<SearchResult> results;
  results.reserve(accumulators.size());

  for (const auto &[document_index, accumulated_score] : accumulators) {
    results.push_back({
        .document_id = documents_[document_index].id,
        .score = accumulated_score.score,
        .matched_terms = accumulated_score.matched_terms,
    });
  }

  const auto desired_size = std::min(options.top_k, results.size());
  if (desired_size == 0) {
    return {};
  }

  const auto ranking = [](const SearchResult &left, const SearchResult &right) {
    if (left.score == right.score) {
      if (left.matched_terms == right.matched_terms) {
        return left.document_id < right.document_id;
      }

      return left.matched_terms > right.matched_terms;
    }

    return left.score > right.score;
  };

  std::partial_sort(results.begin(),
                    results.begin() + static_cast<std::ptrdiff_t>(desired_size),
                    results.end(),
                    ranking);
  results.resize(desired_size);
  return results;
}

void PublishedLexicalIndex::publish_segment(InvertedIndexSegment segment) {
  segments_.push_back(
      std::make_shared<const InvertedIndexSegment>(std::move(segment)));
}

std::vector<SearchResult> PublishedLexicalIndex::search(std::string_view query,
                                                        SearchOptions options) const {
  if (options.top_k == 0) {
    return {};
  }

  const auto query_terms = tokenizer_.tokenize(query);
  if (query_terms.empty()) {
    return {};
  }

  std::vector<SearchResult> merged_results;
  for (const auto &segment : segments_) {
    auto segment_results = segment->search(query_terms, options);
    merged_results.insert(merged_results.end(),
                          std::make_move_iterator(segment_results.begin()),
                          std::make_move_iterator(segment_results.end()));
  }

  if (merged_results.empty()) {
    return {};
  }

  const auto ranking = [](const SearchResult &left, const SearchResult &right) {
    if (left.score == right.score) {
      if (left.matched_terms == right.matched_terms) {
        return left.document_id < right.document_id;
      }

      return left.matched_terms > right.matched_terms;
    }

    return left.score > right.score;
  };

  const auto desired_size = std::min(options.top_k, merged_results.size());
  std::partial_sort(merged_results.begin(),
                    merged_results.begin() +
                        static_cast<std::ptrdiff_t>(desired_size),
                    merged_results.end(),
                    ranking);
  merged_results.resize(desired_size);
  return merged_results;
}

std::size_t PublishedLexicalIndex::segment_count() const {
  return segments_.size();
}

std::size_t PublishedLexicalIndex::total_document_count() const {
  std::size_t total_documents = 0;
  for (const auto &segment : segments_) {
    total_documents += segment->document_count();
  }

  return total_documents;
}

std::size_t PublishedLexicalIndex::total_unique_term_count() const {
  std::size_t total_terms = 0;
  for (const auto &segment : segments_) {
    total_terms += segment->unique_term_count();
  }

  return total_terms;
}

} // namespace kestral
