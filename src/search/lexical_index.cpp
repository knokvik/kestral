#include "kestral/search/lexical_index.hpp"
#include "kestral/search/vbyte.hpp"

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

  std::vector<std::uint8_t> postings_buffer;
  std::vector<std::uint8_t> skips_buffer;
  TransparentStringMap<TermMetadata> term_dict;

  constexpr std::size_t kSkipInterval = 128;

  for (const auto &[term, posting_list] : postings_) {
    const std::uint32_t df = static_cast<std::uint32_t>(posting_list.doc_indexes.size());
    term_dict[term] = {
        .postings_offset = postings_buffer.size(),
        .skips_offset = skips_buffer.size(),
        .document_frequency = df,
    };

    std::uint32_t prev_doc_index = 0;
    std::uint32_t prev_skip_doc_index = 0;
    std::size_t prev_postings_offset = postings_buffer.size();

    for (std::size_t i = 0; i < df; ++i) {
      if (i > 0 && i % kSkipInterval == 0) {
        // Write skip entry: delta_doc_id, delta_postings_offset
        const std::uint32_t max_doc_id = posting_list.doc_indexes[i - 1];
        encode_vbyte(max_doc_id - prev_skip_doc_index, skips_buffer);
        encode_vbyte(static_cast<std::uint32_t>(postings_buffer.size() - prev_postings_offset), skips_buffer);
        
        prev_skip_doc_index = max_doc_id;
        prev_postings_offset = postings_buffer.size();
      }

      const std::uint32_t doc_index = posting_list.doc_indexes[i];
      const std::uint32_t delta = doc_index - prev_doc_index;
      encode_vbyte(delta, postings_buffer);
      encode_vbyte(posting_list.term_frequencies[i], postings_buffer);
      prev_doc_index = doc_index;
    }
  }

  return InvertedIndexSegment(std::move(tokenizer_),
                              std::move(documents_),
                              std::move(term_dict),
                              std::move(postings_buffer),
                              std::move(skips_buffer),
                              average_document_length);
}

void LexicalSegmentBuilder::index_document(const Document &document) {
  // Zero-copy tokenization: tokens are string_views into scratch buffers.
  tokenizer_.tokenize_views(document.title, scratch_title_, title_views_);
  tokenizer_.tokenize_views(document.body, scratch_body_, body_views_);

  // Reuse the per-document term frequency map (clear, don't deallocate).
  term_freq_scratch_.clear();
  term_freq_scratch_.reserve(title_views_.size() + body_views_.size());

  std::uint32_t weighted_length = 0;
  add_tokens(title_views_, kTitleWeight, term_freq_scratch_, weighted_length);
  add_tokens(body_views_, kBodyWeight, term_freq_scratch_, weighted_length);

  const auto document_index = static_cast<std::uint32_t>(documents_.size());
  documents_.push_back({
      .id = document.id,
      .weighted_length = weighted_length,
  });
  total_document_length_ += weighted_length;

  // Insert into the postings map. Transparent hash lets us look up with
  // string_view without constructing a temporary std::string.
  for (const auto &[term_view, frequency] : term_freq_scratch_) {
    auto it = postings_.find(term_view);
    if (it == postings_.end()) {
      // Only allocate a std::string for genuinely new terms.
      it = postings_.emplace(std::string(term_view), BuilderPostingList{})
               .first;
    }
    it->second.doc_indexes.push_back(document_index);
    it->second.term_frequencies.push_back(frequency);
  }
}

void LexicalSegmentBuilder::add_tokens(
    std::span<const std::string_view> tokens, std::uint16_t weight,
    std::unordered_map<std::string_view, std::uint16_t> &term_frequencies,
    std::uint32_t &weighted_length) const {
  weighted_length += static_cast<std::uint32_t>(tokens.size()) * weight;

  for (const auto &token : tokens) {
    term_frequencies[token] += weight;
  }
}

// ---------------------------------------------------------------------------
// PostingListIterator
// ---------------------------------------------------------------------------
class PostingListIterator {
public:
  PostingListIterator(const std::uint8_t *postings_data,
                      const std::uint8_t *skips_data,
                      std::size_t document_frequency)
      : postings_ptr_(postings_data),
        skips_ptr_(skips_data),
        remaining_docs_(document_frequency) {
    if (remaining_docs_ > 0) {
      next();
    }
  }

  [[nodiscard]] bool done() const { return remaining_docs_ == 0 && current_doc_ == 0; /* use 0 as sentinel when done */ }
  [[nodiscard]] std::uint32_t doc_id() const { return current_doc_; }
  [[nodiscard]] std::uint32_t term_frequency() const { return current_tf_; }

  void next() {
    if (remaining_docs_ == 0) {
      current_doc_ = 0; // Sentinel for done
      return;
    }
    
    std::uint32_t delta = 0;
    postings_ptr_ = decode_vbyte(postings_ptr_, delta);
    current_doc_ += delta;
    
    postings_ptr_ = decode_vbyte(postings_ptr_, current_tf_);
    remaining_docs_--;
    skip_counter_++;
  }

  void seek(std::uint32_t target_doc_id) {
    // 1. Advance through skips if beneficial
    while (skips_ptr_ && remaining_docs_ > 128) {
      // Decode skip entry without advancing the pointer yet
      const std::uint8_t *temp_skips = skips_ptr_;
      std::uint32_t max_doc_delta = 0;
      std::uint32_t postings_delta = 0;
      temp_skips = decode_vbyte(temp_skips, max_doc_delta);
      temp_skips = decode_vbyte(temp_skips, postings_delta);

      std::uint32_t next_max_doc = current_skip_doc_ + max_doc_delta;
      if (next_max_doc < target_doc_id) {
        // Safe to skip this block
        skips_ptr_ = temp_skips;
        current_skip_doc_ = next_max_doc;
        postings_ptr_ += postings_delta;
        remaining_docs_ -= 128; // kSkipInterval
        skip_counter_ = 0;
        
        // After skipping, our current_doc_ state needs to reflect the new block base
        // But since we skipped, the next decode will give a delta from the PREVIOUS block's max doc.
        // Wait, VByte deltas are strictly sequential. The block starts with a delta from the previous max doc.
        // So we update current_doc_ to the previous block's max doc!
        current_doc_ = current_skip_doc_;
      } else {
        break; // Target is within this block
      }
    }

    // 2. Linear scan for the rest
    while (!done() && current_doc_ < target_doc_id) {
      next();
    }
  }

private:
  const std::uint8_t *postings_ptr_;
  const std::uint8_t *skips_ptr_;
  std::size_t remaining_docs_;
  
  std::uint32_t current_doc_ = 0;
  std::uint32_t current_tf_ = 0;
  std::uint32_t current_skip_doc_ = 0;
  std::size_t skip_counter_ = 0;
};

// ---------------------------------------------------------------------------
// InvertedIndexSegment
// ---------------------------------------------------------------------------

InvertedIndexSegment::InvertedIndexSegment(
    Tokenizer tokenizer,
    std::vector<IndexedLexicalDocument> documents,
    TransparentStringMap<TermMetadata> term_dict,
    std::vector<std::uint8_t> postings_buffer,
    std::vector<std::uint8_t> skips_buffer,
    double average_document_length)
    : tokenizer_(std::move(tokenizer)),
      documents_(std::move(documents)),
      term_dict_(std::move(term_dict)),
      postings_buffer_(std::move(postings_buffer)),
      skips_buffer_(std::move(skips_buffer)),
      average_document_length_(average_document_length) {}

std::size_t InvertedIndexSegment::document_count() const {
  return documents_.size();
}

std::size_t InvertedIndexSegment::unique_term_count() const {
  return term_dict_.size();
}

double InvertedIndexSegment::average_document_length() const {
  return average_document_length_;
}

std::vector<SearchResult> InvertedIndexSegment::search(
    std::span<const std::string_view> query_terms,
    SearchOptions options) const {
  if (query_terms.empty() || documents_.empty()) {
    return {};
  }

  // Deduplicate and weight query terms.
  std::unordered_map<std::string_view, std::uint32_t> query_term_weights;
  query_term_weights.reserve(query_terms.size());
  for (const auto &term : query_terms) {
    query_term_weights[term] += 1;
  }

  std::unordered_map<std::uint32_t, AccumulatedScore> accumulators;

  std::vector<PostingListIterator> iterators;
  std::vector<std::uint32_t> query_weights;
  std::vector<std::uint32_t> document_frequencies;
  
  iterators.reserve(query_term_weights.size());
  query_weights.reserve(query_term_weights.size());
  document_frequencies.reserve(query_term_weights.size());

  for (const auto &[term, query_weight] : query_term_weights) {
    const auto term_dict_iterator = term_dict_.find(term);
    if (term_dict_iterator == term_dict_.end()) {
      if (options.require_all_terms) {
        return {}; // One term is missing, AND query yields nothing
      }
      continue;
    }

    const auto &meta = term_dict_iterator->second;
    const std::uint8_t *postings_ptr = postings_buffer_.data() + meta.postings_offset;
    const std::uint8_t *skips_ptr = meta.skips_offset < skips_buffer_.size() 
        ? skips_buffer_.data() + meta.skips_offset 
        : nullptr;

    iterators.emplace_back(postings_ptr, skips_ptr, meta.document_frequency);
    query_weights.push_back(query_weight);
    document_frequencies.push_back(meta.document_frequency);
  }

  if (iterators.empty()) {
    return {};
  }

  if (options.require_all_terms) {
    // Sort iterators by document frequency (shortest first) to optimize skip pointers
    std::vector<std::size_t> iterator_order(iterators.size());
    for (std::size_t i = 0; i < iterators.size(); ++i) iterator_order[i] = i;
    std::sort(iterator_order.begin(), iterator_order.end(), [&](std::size_t a, std::size_t b) {
      return document_frequencies[a] < document_frequencies[b];
    });

    auto &first_it = iterators[iterator_order[0]];
    while (!first_it.done()) {
      const std::uint32_t candidate_doc = first_it.doc_id();
      bool match = true;

      for (std::size_t i = 1; i < iterators.size(); ++i) {
        auto &other_it = iterators[iterator_order[i]];
        other_it.seek(candidate_doc);
        
        if (other_it.done() || other_it.doc_id() != candidate_doc) {
          match = false;
          // Advance first_it to the document that other_it is currently at
          if (!other_it.done()) {
            first_it.seek(other_it.doc_id());
          } else {
            // Force first_it to finish
            while (!first_it.done()) first_it.next();
          }
          break;
        }
      }

      if (match) {
        const auto &document = documents_[candidate_doc];
        
        // Soft delete check
        if (!options.deleted_docs || !options.deleted_docs->is_deleted(document.id)) {
          auto &accumulated_score = accumulators[candidate_doc];
          
          for (std::size_t i = 0; i < iterators.size(); ++i) {
            const std::size_t orig_idx = iterator_order[i];
            accumulated_score.score +=
                static_cast<double>(query_weights[orig_idx]) *
                bm25_term_score(static_cast<std::uint16_t>(iterators[orig_idx].term_frequency()),
                                document.weighted_length,
                                average_document_length_,
                                documents_.size(),
                                document_frequencies[orig_idx],
                                options);
            accumulated_score.matched_terms += 1;
            iterators[orig_idx].next();
          }
        } else {
          // It's deleted, but we still need to advance the iterators
          for (std::size_t i = 0; i < iterators.size(); ++i) {
            iterators[i].next();
          }
        }
      }
    }
  } else {
    // TAAT (Term-At-A-Time) for OR queries
    for (std::size_t i = 0; i < iterators.size(); ++i) {
      auto &it = iterators[i];
      const auto query_weight = query_weights[i];
      const auto df = document_frequencies[i];
      
      while (!it.done()) {
        const std::uint32_t doc_index = it.doc_id();
        const std::uint32_t term_frequency = it.term_frequency();
        const auto &document = documents_[doc_index];

        // Soft delete check
        if (!options.deleted_docs || !options.deleted_docs->is_deleted(document.id)) {
          auto &accumulated_score = accumulators[doc_index];
          accumulated_score.score +=
              static_cast<double>(query_weight) *
              bm25_term_score(static_cast<std::uint16_t>(term_frequency),
                              document.weighted_length,
                              average_document_length_,
                              documents_.size(),
                              df,
                              options);
          accumulated_score.matched_terms += 1;
        }
        it.next();
      }
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

  const auto ranking = [](const SearchResult &left,
                          const SearchResult &right) {
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

// ---------------------------------------------------------------------------
// PublishedLexicalIndex
// ---------------------------------------------------------------------------

void PublishedLexicalIndex::publish_segment(InvertedIndexSegment segment) {
  auto ptr = std::make_shared<const InvertedIndexSegment>(std::move(segment));
  std::unique_lock lock(mutex_);
  segments_.push_back(std::move(ptr));
}

std::vector<SearchResult>
PublishedLexicalIndex::search(std::string_view query,
                              SearchOptions options) const {
  std::vector<std::string_view> query_terms;
  // Let's create a local Tokenizer for thread-safety during search
  Tokenizer local_tokenizer;
  std::string scratch;
  local_tokenizer.tokenize_views(query, scratch, query_terms);

  std::vector<std::shared_ptr<const InvertedIndexSegment>> local_segments;
  {
    std::shared_lock lock(mutex_);
    local_segments = segments_; // Fast atomic copy of shared_ptrs
  }

  std::vector<SearchResult> merged_results;
  for (const auto &segment : local_segments) {
    auto segment_results = segment->search(query_terms, options);
    merged_results.insert(merged_results.end(),
                          std::make_move_iterator(segment_results.begin()),
                          std::make_move_iterator(segment_results.end()));
  }

  if (merged_results.empty()) {
    return {};
  }

  const auto ranking = [](const SearchResult &left,
                          const SearchResult &right) {
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
  std::shared_lock lock(mutex_);
  return segments_.size();
}

std::size_t PublishedLexicalIndex::total_document_count() const {
  std::vector<std::shared_ptr<const InvertedIndexSegment>> local_segments;
  {
    std::shared_lock lock(mutex_);
    local_segments = segments_;
  }
  std::size_t total_documents = 0;
  for (const auto &segment : local_segments) {
    total_documents += segment->document_count();
  }
  return total_documents;
}

std::size_t PublishedLexicalIndex::total_unique_term_count() const {
  std::vector<std::shared_ptr<const InvertedIndexSegment>> local_segments;
  {
    std::shared_lock lock(mutex_);
    local_segments = segments_;
  }
  std::size_t total_terms = 0;
  for (const auto &segment : local_segments) {
    total_terms += segment->unique_term_count();
  }
  return total_terms;
}

} // namespace kestral
