#include "kestral/ingest/synthetic_corpus.hpp"

#include <stdexcept>
#include <utility>

namespace kestral {

SyntheticCorpusGenerator::SyntheticCorpusGenerator(SyntheticCorpusConfig config)
    : config_(std::move(config)),
      next_document_id_(config_.first_document_id),
      next_timestamp_(config_.first_timestamp),
      random_engine_(config_.seed) {
  if (config_.vocabulary.empty()) {
    throw std::invalid_argument("SyntheticCorpusGenerator needs a vocabulary");
  }

  if (config_.categories.empty()) {
    throw std::invalid_argument("SyntheticCorpusGenerator needs categories");
  }

  term_picker_ = std::uniform_int_distribution<std::size_t>(
      0, config_.vocabulary.size() - 1);
  category_picker_ = std::uniform_int_distribution<std::size_t>(
      0, config_.categories.size() - 1);
  embedding_value_generator_ = std::uniform_real_distribution<float>(-1.0f, 1.0f);
}

void SyntheticCorpusGenerator::generate_next_batch(DocumentBatch &batch,
                                                   std::size_t document_count) {
  batch.clear();
  batch.reserve(document_count);

  for (std::size_t index = 0; index < document_count; ++index) {
    batch.add(make_document());
  }
}

void SyntheticCorpusGenerator::reset() {
  next_document_id_ = config_.first_document_id;
  next_timestamp_ = config_.first_timestamp;
  random_engine_.seed(config_.seed);
}

Document SyntheticCorpusGenerator::make_document() {
  Document document;
  document.id = next_document_id_++;
  document.timestamp = next_timestamp_;
  document.title = make_title();
  document.body = make_body();

  document.embedding.resize(config_.embedding_dimension);
  for (auto &val : document.embedding) {
    val = embedding_value_generator_(random_engine_);
  }

  next_timestamp_ += config_.timestamp_step_seconds;
  return document;
}

std::string SyntheticCorpusGenerator::make_title() {
  return make_phrase(config_.title_terms_per_document, true);
}

std::string SyntheticCorpusGenerator::make_body() {
  return make_phrase(config_.body_terms_per_document, false);
}

std::string SyntheticCorpusGenerator::make_phrase(
    std::size_t term_count,
    bool include_category_prefix) {
  std::string phrase;
  phrase.reserve(term_count * 10 + 24);

  if (include_category_prefix) {
    phrase.append(config_.categories[category_picker_(random_engine_)]);
  }

  for (std::size_t index = 0; index < term_count; ++index) {
    if (!phrase.empty()) {
      phrase.push_back(' ');
    }

    append_random_term(phrase);
  }

  return phrase;
}

void SyntheticCorpusGenerator::append_random_term(std::string &output) {
  output.append(config_.vocabulary[term_picker_(random_engine_)]);
}

} // namespace kestral
