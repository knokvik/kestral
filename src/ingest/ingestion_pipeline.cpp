#include "kestral/ingest/ingestion_pipeline.hpp"
#include "kestral/core/concurrent_queue.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <thread>
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
  if (config_.num_threads > 0) {
    return run_parallel();
  }
  return run_sequential();
}

IngestionMetrics IngestionPipeline::run_sequential() {
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

IngestionMetrics IngestionPipeline::run_parallel() {
  // The parallel pipeline uses a producer/consumer pattern:
  //   - The main thread generates batches (producer)
  //   - N worker threads consume batches (write to store + feed consumers)
  //
  // We use a bounded ConcurrentQueue to regulate backpressure: if workers
  // can't keep up, the producer blocks until a slot opens.

  const std::size_t num_workers = config_.num_threads;
  const std::size_t queue_depth = num_workers * 4;

  // Each batch in the queue is a shared_ptr so we avoid expensive moves
  // of the document vector while keeping ownership clear.
  ConcurrentQueue<std::shared_ptr<DocumentBatch>> batch_queue(queue_depth);

  std::atomic<std::size_t> total_documents_processed{0};
  std::atomic<std::size_t> total_batches_processed{0};

  std::mutex latency_mutex;
  std::vector<double> batch_latencies_ms;
  batch_latencies_ms.reserve(
      (config_.total_documents + config_.batch_size - 1) / config_.batch_size);

  // --- Worker threads ---
  // Each worker pulls batches from the queue, writes to the store,
  // and feeds any registered consumers.
  std::vector<std::thread> workers;
  workers.reserve(num_workers);

  for (std::size_t i = 0; i < num_workers; ++i) {
    workers.emplace_back([this, &batch_queue, &total_documents_processed,
                          &total_batches_processed, &latency_mutex,
                          &batch_latencies_ms] {
      while (auto maybe_batch = batch_queue.pop()) {
        auto &batch = *maybe_batch;
        const auto batch_started_at = std::chrono::steady_clock::now();

        store_.write_batch(batch->documents());
        for (auto *consumer : config_.consumers) {
          if (consumer != nullptr) {
            consumer->consume(batch->documents());
          }
        }

        const auto batch_finished_at = std::chrono::steady_clock::now();
        const double latency_ms =
            to_milliseconds(batch_finished_at - batch_started_at);

        total_documents_processed.fetch_add(batch->size(),
                                            std::memory_order_relaxed);
        total_batches_processed.fetch_add(1, std::memory_order_relaxed);

        {
          std::lock_guard lock(latency_mutex);
          batch_latencies_ms.push_back(latency_ms);
        }
      }
    });
  }

  // --- Producer (main thread) ---
  const auto started_at = std::chrono::steady_clock::now();
  std::size_t remaining_documents = config_.total_documents;

  while (remaining_documents > 0) {
    const auto documents_this_batch =
        std::min(config_.batch_size, remaining_documents);

    auto batch = std::make_shared<DocumentBatch>(documents_this_batch);
    generator_.generate_next_batch(*batch, documents_this_batch);

    if (!batch_queue.push(std::move(batch))) {
      break; // queue was closed unexpectedly
    }
    remaining_documents -= documents_this_batch;
  }

  // Signal workers that no more batches are coming and wait for drain.
  batch_queue.close();
  for (auto &worker : workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }

  store_.flush();

  const auto finished_at = std::chrono::steady_clock::now();

  // --- Assemble metrics ---
  IngestionMetrics metrics;
  metrics.documents_written =
      total_documents_processed.load(std::memory_order_relaxed);
  metrics.batches_written =
      total_batches_processed.load(std::memory_order_relaxed);
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
