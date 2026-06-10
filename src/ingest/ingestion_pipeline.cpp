#include "kestral/ingest/ingestion_pipeline.hpp"

#include <algorithm>
#include <chrono>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {

double to_milliseconds(std::chrono::steady_clock::duration duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}

double percentile_95(std::vector<double> samples) {
  if (samples.empty()) {
    return 0.0;
  }

  std::sort(samples.begin(), samples.end());
  const auto percentile_index =
      static_cast<std::size_t>(0.95 * static_cast<double>(samples.size() - 1));
  return samples[percentile_index];
}

} // namespace

namespace kestral {

IngestionPipeline::IngestionPipeline(SyntheticCorpusGenerator &generator,
                                     DocumentStore &store,
                                     IngestionPipelineConfig config)
    : generator_(generator), store_(store), config_(config) {
  if (config_.batch_size == 0) {
    throw std::invalid_argument("IngestionPipeline batch_size must be > 0");
  }
}

IngestionMetrics IngestionPipeline::run() {
  IngestionMetrics metrics;
  DocumentBatch batch(config_.batch_size);
  std::vector<double> batch_latencies_ms;
  batch_latencies_ms.reserve(
      (config_.total_documents + config_.batch_size - 1) / config_.batch_size);

  const auto started_at = std::chrono::steady_clock::now();
  std::size_t remaining_documents = config_.total_documents;

  while (remaining_documents > 0) {
    const auto documents_this_batch =
        std::min(config_.batch_size, remaining_documents);

    const auto batch_started_at = std::chrono::steady_clock::now();
    generator_.generate_next_batch(batch, documents_this_batch);
    store_.write_batch(batch.documents());
    for (auto *consumer : config_.consumers) {
      if (consumer != nullptr) {
        consumer->consume(batch.documents());
      }
    }
    const auto batch_finished_at = std::chrono::steady_clock::now();

    batch_latencies_ms.push_back(
        to_milliseconds(batch_finished_at - batch_started_at));
    metrics.documents_written += documents_this_batch;
    metrics.batches_written += 1;
    remaining_documents -= documents_this_batch;
  }

  store_.flush();

  const auto finished_at = std::chrono::steady_clock::now();
  metrics.elapsed_seconds =
      std::chrono::duration<double>(finished_at - started_at).count();
  metrics.documents_per_second =
      metrics.elapsed_seconds > 0.0
          ? static_cast<double>(metrics.documents_written) /
                metrics.elapsed_seconds
          : 0.0;

  if (!batch_latencies_ms.empty()) {
    metrics.average_batch_latency_ms =
        std::accumulate(batch_latencies_ms.begin(),
                        batch_latencies_ms.end(),
                        0.0) /
        static_cast<double>(batch_latencies_ms.size());
    metrics.p95_batch_latency_ms = percentile_95(batch_latencies_ms);
    metrics.max_batch_latency_ms =
        *std::max_element(batch_latencies_ms.begin(), batch_latencies_ms.end());
  }

  return metrics;
}

} // namespace kestral
