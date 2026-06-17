#pragma once

#include "kestral/core/document.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace kestral {

struct SyntheticCorpusConfig {
  std::size_t title_terms_per_document = 6;
  std::size_t body_terms_per_document = 96;
  std::uint64_t first_document_id = 1;
  std::uint64_t first_timestamp = 1704067200;
  std::uint64_t timestamp_step_seconds = 3;
  std::uint64_t seed = 42;
  std::size_t embedding_dimension = 128;
  std::vector<std::string> vocabulary = {
      "vector",    "index",     "pipeline", "latency",   "ranking",
      "segment",   "storage",   "recall",   "fusion",    "posting",
      "snapshot",  "query",     "freshness","compaction","allocator",
      "throughput","replica",   "token",    "search",    "document"};
  std::vector<std::string> categories = {
      "systems", "search", "infra", "finance", "ml"};
};

class SyntheticCorpusGenerator {
public:
  explicit SyntheticCorpusGenerator(SyntheticCorpusConfig config = {});

  void generate_next_batch(DocumentBatch &batch, std::size_t document_count);
  void reset();

private:
  [[nodiscard]] Document make_document();
  [[nodiscard]] std::string make_title();
  [[nodiscard]] std::string make_body();
  [[nodiscard]] std::string make_phrase(std::size_t term_count,
                                        bool include_category_prefix);
  void append_random_term(std::string &output);

  SyntheticCorpusConfig config_;
  std::uint64_t next_document_id_;
  std::uint64_t next_timestamp_;
  std::mt19937_64 random_engine_;
  std::uniform_int_distribution<std::size_t> term_picker_;
  std::uniform_int_distribution<std::size_t> category_picker_;
  std::uniform_real_distribution<float> embedding_value_generator_;
};

} // namespace kestral
