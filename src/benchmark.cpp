#include "kestral/ingest/ingestion_pipeline.hpp"
#include "kestral/ingest/synthetic_corpus.hpp"
#include "kestral/search/lexical_index.hpp"
#include "kestral/search/tokenizer.hpp"
#include "kestral/search/vector_index.hpp"
#include "kestral/search/hybrid_search.hpp"
#include "kestral/storage/document_store.hpp"

#include <benchmark/benchmark.h>

#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

std::filesystem::path make_benchmark_path(int batch_size) {
  return std::filesystem::temp_directory_path() /
         ("kestral-bench-" + std::to_string(batch_size));
}

void BM_BatchedIngestion(benchmark::State &state) {
  const std::size_t batch_size = static_cast<std::size_t>(state.range(0));
  constexpr std::size_t kDocumentCount = 100000;
  const auto db_path = make_benchmark_path(static_cast<int>(batch_size));

  for (auto _ : state) {
    state.PauseTiming();
    std::filesystem::remove_all(db_path);
    state.ResumeTiming();

    kestral::IngestionMetrics metrics;
    {
      kestral::SyntheticCorpusGenerator generator;
      kestral::DocumentStore store({
          .db_path = db_path.string(),
          .write_buffer_size_mb = 128,
      });
      kestral::IngestionPipeline pipeline(
          generator,
          store,
          {
              .total_documents = kDocumentCount,
              .batch_size = batch_size,
          });

      metrics = pipeline.run();
    }

    auto docs_per_second = metrics.documents_per_second;
    benchmark::DoNotOptimize(docs_per_second);
    state.counters["docs_per_sec"] = metrics.documents_per_second;
    state.counters["avg_batch_ms"] = metrics.average_batch_latency_ms;
    state.counters["p95_batch_ms"] = metrics.p95_batch_latency_ms;

    state.PauseTiming();
    std::filesystem::remove_all(db_path);
    state.ResumeTiming();
  }
}

void BM_LexicalSearch(benchmark::State &state) {
  const std::size_t document_count = static_cast<std::size_t>(state.range(0));
  constexpr std::size_t kBatchSize = 4096;

  kestral::SyntheticCorpusGenerator generator;
  kestral::DocumentBatch batch(kBatchSize);
  kestral::LexicalSegmentBuilder builder;

  std::size_t remaining_documents = document_count;
  while (remaining_documents > 0) {
    const auto documents_this_batch = std::min(kBatchSize, remaining_documents);
    generator.generate_next_batch(batch, documents_this_batch);
    builder.consume(batch.documents());
    remaining_documents -= documents_this_batch;
  }

  kestral::PublishedLexicalIndex lexical_index;
  lexical_index.publish_segment(std::move(builder).build());

  const std::vector<std::string> queries = {
      "vector search",
      "segment latency",
      "allocator throughput query",
      "freshness ranking storage",
  };
  std::size_t query_index = 0;
  std::uint64_t checksum = 0;

  for (auto _ : state) {
    const auto &query = queries[query_index % queries.size()];
    const auto results = lexical_index.search(query, {.top_k = 10});
    if (!results.empty()) {
      checksum ^= results.front().document_id;
    }
    query_index += 1;
  }

  state.counters["docs"] = static_cast<double>(document_count);
  state.counters["segments"] =
      static_cast<double>(lexical_index.segment_count());
  state.counters["checksum"] = static_cast<double>(checksum);
}

} // namespace

BENCHMARK(BM_BatchedIngestion)->Arg(512)->Arg(2048)->Arg(8192);
BENCHMARK(BM_LexicalSearch)->Arg(10000)->Arg(50000)->Arg(100000);

void BM_ParallelIngestion(benchmark::State &state) {
  const std::size_t num_threads = static_cast<std::size_t>(state.range(0));
  constexpr std::size_t kDocumentCount = 100000;
  constexpr std::size_t kBatchSize = 4096;
  const auto db_path = make_benchmark_path(static_cast<int>(num_threads) + 100);

  for (auto _ : state) {
    state.PauseTiming();
    std::filesystem::remove_all(db_path);
    state.ResumeTiming();

    kestral::IngestionMetrics metrics;
    {
      kestral::SyntheticCorpusGenerator generator;
      kestral::DocumentStore store({
          .db_path = db_path.string(),
          .write_buffer_size_mb = 128,
      });
      kestral::IngestionPipeline pipeline(
          generator,
          store,
          {
              .total_documents = kDocumentCount,
              .batch_size = kBatchSize,
              .num_threads = num_threads,
          });

      metrics = pipeline.run();
    }

    auto docs_per_second = metrics.documents_per_second;
    benchmark::DoNotOptimize(docs_per_second);
    state.counters["docs_per_sec"] = metrics.documents_per_second;
    state.counters["avg_batch_ms"] = metrics.average_batch_latency_ms;
    state.counters["p95_batch_ms"] = metrics.p95_batch_latency_ms;

    state.PauseTiming();
    std::filesystem::remove_all(db_path);
    state.ResumeTiming();
  }
}

BENCHMARK(BM_ParallelIngestion)->Arg(2)->Arg(4)->Arg(8);

static void BM_VectorIndexIngestion(benchmark::State &state) {
  std::size_t num_docs = state.range(0);
  kestral::DocumentBatch batch;
  std::mt19937 gen(42);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
  
  for (std::size_t i = 0; i < num_docs; ++i) {
    std::vector<float> vec(128);
    for (float &v : vec) v = dist(gen);
    batch.add({.id = static_cast<std::uint64_t>(i), .embedding = vec});
  }

  for (auto _ : state) {
    kestral::VectorIndex index(128);
    index.consume(batch.documents());
    benchmark::DoNotOptimize(index);
  }
}
BENCHMARK(BM_VectorIndexIngestion)->Arg(10000)->Arg(50000)->Arg(100000);

static void BM_VectorIndexSearch(benchmark::State &state) {
  std::size_t num_docs = state.range(0);
  kestral::DocumentBatch batch;
  std::mt19937 gen(42);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
  
  for (std::size_t i = 0; i < num_docs; ++i) {
    std::vector<float> vec(128);
    for (float &v : vec) v = dist(gen);
    batch.add({.id = static_cast<std::uint64_t>(i), .embedding = vec});
  }

  kestral::VectorIndex index(128);
  index.consume(batch.documents());

  std::vector<float> query(128);
  for (float &v : query) v = dist(gen);

  for (auto _ : state) {
    for (int i = 0; i < 100; ++i) {
      auto results = index.search(query, 10);
      benchmark::DoNotOptimize(results);
    }
  }
}
BENCHMARK(BM_VectorIndexSearch)->Arg(10000)->Arg(50000)->Arg(100000)->Iterations(1);

static void BM_HybridSearch(benchmark::State &state) {
  std::size_t num_docs = state.range(0);
  kestral::DocumentBatch batch;
  std::mt19937 gen(42);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
  
  for (std::size_t i = 0; i < num_docs; ++i) {
    std::vector<float> vec(128);
    for (float &v : vec) v = dist(gen);
    batch.add({.id = static_cast<std::uint64_t>(i),
               .title = "Hybrid benchmark title",
               .body = "This is a body full of hybrid content to index lexical keywords",
               .embedding = vec});
  }

  kestral::LexicalSegmentBuilder lexical_builder;
  kestral::VectorIndex vector_index(128);
  lexical_builder.consume(batch.documents());
  vector_index.consume(batch.documents());

  kestral::PublishedLexicalIndex lexical_index;
  lexical_index.publish_segment(std::move(lexical_builder).build());

  kestral::HybridSearchEngine engine(lexical_index, vector_index);

  std::vector<float> query_v(128);
  for (float &v : query_v) v = dist(gen);

  for (auto _ : state) {
    for (int i = 0; i < 100; ++i) {
      auto results = engine.search("hybrid keyword content", query_v, 10);
      benchmark::DoNotOptimize(results);
    }
  }
}
BENCHMARK(BM_HybridSearch)->Arg(10000)->Arg(50000)->Arg(100000)->Iterations(1);

static void BM_QueryCacheHit(benchmark::State &state) {
  kestral::QueryCache cache(1000, std::chrono::minutes(5));
  kestral::CacheKey key{"test_query", 10, false};
  
  std::vector<kestral::SearchResult> mock_results(10);
  cache.put(key, mock_results);

  for (auto _ : state) {
    auto results = cache.get(key);
    benchmark::DoNotOptimize(results);
  }
}
BENCHMARK(BM_QueryCacheHit);

// ---------------------------------------------------------------------------
// Tokenizer micro-benchmark: allocating vs zero-copy
// ---------------------------------------------------------------------------
namespace {

void BM_TokenizerAllocating(benchmark::State &state) {
  // Build a realistic-length text string from synthetic corpus words.
  const std::string text =
      "The vector search engine processes documents with high throughput and "
      "low latency while maintaining freshness guarantees for segment merging "
      "and compaction across multiple storage tiers in the indexing pipeline. "
      "Allocator throughput ranking storage query vector segment latency.";

  kestral::Tokenizer tokenizer;
  for (auto _ : state) {
    auto tokens = tokenizer.tokenize(text);
    benchmark::DoNotOptimize(tokens.data());
    benchmark::ClobberMemory();
  }
}

void BM_TokenizerZeroCopy(benchmark::State &state) {
  const std::string text =
      "The vector search engine processes documents with high throughput and "
      "low latency while maintaining freshness guarantees for segment merging "
      "and compaction across multiple storage tiers in the indexing pipeline. "
      "Allocator throughput ranking storage query vector segment latency.";

  kestral::Tokenizer tokenizer;
  std::string scratch;
  std::vector<std::string_view> tokens;
  for (auto _ : state) {
    tokenizer.tokenize_views(text, scratch, tokens);
    benchmark::DoNotOptimize(tokens.data());
    benchmark::ClobberMemory();
  }
}

// ---------------------------------------------------------------------------
// Segment-builder-only ingestion (no RocksDB, isolates indexing performance)
// ---------------------------------------------------------------------------
void BM_SegmentBuilderIngestion(benchmark::State &state) {
  const std::size_t document_count = static_cast<std::size_t>(state.range(0));
  constexpr std::size_t kBatchSize = 4096;

  for (auto _ : state) {
    kestral::SyntheticCorpusGenerator generator;
    kestral::DocumentBatch batch(kBatchSize);
    kestral::LexicalSegmentBuilder builder;

    std::size_t remaining = document_count;
    while (remaining > 0) {
      const auto count = std::min(kBatchSize, remaining);
      generator.generate_next_batch(batch, count);
      builder.consume(batch.documents());
      remaining -= count;
    }

    auto segment = std::move(builder).build();
    benchmark::DoNotOptimize(segment.document_count());
  }

  state.counters["docs"] = static_cast<double>(document_count);
}

} // namespace

BENCHMARK(BM_TokenizerAllocating);
BENCHMARK(BM_TokenizerZeroCopy);
BENCHMARK(BM_SegmentBuilderIngestion)->Arg(10000)->Arg(50000)->Arg(100000);

BENCHMARK_MAIN();

