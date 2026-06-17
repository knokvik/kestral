#pragma once

#include "kestral/core/document_batch_consumer.hpp"
#include "kestral/ingest/synthetic_corpus.hpp"
#include "kestral/storage/document_store.hpp"

#include <cstddef>
#include <vector>

namespace kestral {

struct IngestionPipelineConfig {
  std::size_t total_documents = 100000;
  std::size_t batch_size = 4096;
  std::size_t num_threads = 0; // 0 = sequential (original path)
  std::vector<DocumentBatchConsumer *> consumers;
};

struct IngestionMetrics {
  std::size_t documents_written = 0;
  std::size_t batches_written = 0;
  double elapsed_seconds = 0.0;
  double documents_per_second = 0.0;
  double average_batch_latency_ms = 0.0;
  double p95_batch_latency_ms = 0.0;
  double max_batch_latency_ms = 0.0;
};

class IngestionPipeline {
public:
  IngestionPipeline(SyntheticCorpusGenerator &generator,
                    DocumentStore &store,
                    IngestionPipelineConfig config);

  [[nodiscard]] IngestionMetrics run();

private:
  [[nodiscard]] IngestionMetrics run_sequential();
  [[nodiscard]] IngestionMetrics run_parallel();

  SyntheticCorpusGenerator &generator_;
  DocumentStore &store_;
  IngestionPipelineConfig config_;
};

} // namespace kestral
