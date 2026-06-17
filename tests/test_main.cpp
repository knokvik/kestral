#include <catch2/catch_test_macros.hpp>
#include "kestral/core/concurrent_queue.hpp"
#include "kestral/ingest/ingestion_pipeline.hpp"
#include "kestral/ingest/synthetic_corpus.hpp"
#include "kestral/search/lexical_index.hpp"
#include "kestral/search/tokenizer.hpp"
#include "kestral/search/vbyte.hpp"
#include "kestral/storage/document_store.hpp"

#include <chrono>
#include <filesystem>
#include <thread>

namespace {

std::filesystem::path make_test_db_path() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("kestral-test-" + std::to_string(nonce));
}

} // namespace

TEST_CASE("Synthetic generator creates readable batches", "[ingest]") {
  kestral::SyntheticCorpusGenerator generator;
  kestral::DocumentBatch batch;

  generator.generate_next_batch(batch, 3);

  REQUIRE(batch.size() == 3);
  REQUIRE(batch.documents()[0].id == 1);
  REQUIRE(batch.documents()[1].id == 2);
  REQUIRE_FALSE(batch.documents()[0].title.empty());
  REQUIRE_FALSE(batch.documents()[0].body.empty());
}

TEST_CASE("Tokenizer normalizes punctuation and case", "[search]") {
  kestral::Tokenizer tokenizer;
  const auto tokens = tokenizer.tokenize("Vector-search, LATENCY! 101");

  REQUIRE(tokens.size() == 4);
  REQUIRE(tokens[0] == "vector");
  REQUIRE(tokens[1] == "search");
  REQUIRE(tokens[2] == "latency");
  REQUIRE(tokens[3] == "101");
}

TEST_CASE("Tokenizer zero-copy views match allocating tokenize",
          "[search]") {
  kestral::Tokenizer tokenizer;
  std::string scratch;
  std::vector<std::string_view> views;

  tokenizer.tokenize_views("Vector-search, LATENCY! 101", scratch, views);

  REQUIRE(views.size() == 4);
  CHECK(views[0] == "vector");
  CHECK(views[1] == "search");
  CHECK(views[2] == "latency");
  CHECK(views[3] == "101");

  // Verify views point into the scratch buffer (zero-copy property).
  for (const auto &view : views) {
    CHECK(view.data() >= scratch.data());
    CHECK(view.data() + view.size() <= scratch.data() + scratch.size());
  }

  // Verify equivalence with the allocating API.
  const auto strings = tokenizer.tokenize("Vector-search, LATENCY! 101");
  REQUIRE(strings.size() == views.size());
  for (std::size_t i = 0; i < strings.size(); ++i) {
    CHECK(strings[i] == views[i]);
  }
}

TEST_CASE("Document store round-trips a batch", "[storage]") {
  const auto db_path = make_test_db_path();
  std::filesystem::remove_all(db_path);

  kestral::SyntheticCorpusGenerator generator;
  kestral::DocumentBatch batch;
  generator.generate_next_batch(batch, 4);

  {
    kestral::DocumentStore store({.db_path = db_path.string()});
    store.write_batch(batch.documents());
    store.flush();

    const auto document = store.read_document(batch.documents()[2].id);
    REQUIRE(document.has_value());
    REQUIRE(document->id == batch.documents()[2].id);
    REQUIRE(document->timestamp == batch.documents()[2].timestamp);
    REQUIRE(document->title == batch.documents()[2].title);
    REQUIRE(document->body == batch.documents()[2].body);
  }

  std::filesystem::remove_all(db_path);
}

TEST_CASE("Ingestion pipeline reports useful metrics", "[pipeline]") {
  const auto db_path = make_test_db_path();
  std::filesystem::remove_all(db_path);

  kestral::SyntheticCorpusGenerator generator;
  {
    kestral::DocumentStore store({.db_path = db_path.string()});
    kestral::IngestionPipeline pipeline(
        generator,
        store,
        {
            .total_documents = 25,
            .batch_size = 8,
        });

    const auto metrics = pipeline.run();

    REQUIRE(metrics.documents_written == 25);
    REQUIRE(metrics.batches_written == 4);
    REQUIRE(metrics.elapsed_seconds > 0.0);
    REQUIRE(metrics.documents_per_second > 0.0);
    REQUIRE(metrics.average_batch_latency_ms >= 0.0);
    REQUIRE(metrics.p95_batch_latency_ms >= 0.0);
    REQUIRE(metrics.max_batch_latency_ms >= metrics.p95_batch_latency_ms);

    const auto first_document = store.read_document(1);
    REQUIRE(first_document.has_value());
  }

  std::filesystem::remove_all(db_path);
}

TEST_CASE("Lexical segment ranks stronger BM25 matches first", "[search]") {
  kestral::DocumentBatch batch;
  batch.add({
      .id = 1,
      .timestamp = 1,
      .title = "Vector search systems",
      .body = "vector search vector search latency throughput",
  });
  batch.add({
      .id = 2,
      .timestamp = 2,
      .title = "Storage notes",
      .body = "storage compaction durability throughput",
  });
  batch.add({
      .id = 3,
      .timestamp = 3,
      .title = "Search ranking",
      .body = "vector ranking search",
  });

  kestral::LexicalSegmentBuilder builder;
  builder.consume(batch.documents());

  kestral::PublishedLexicalIndex index;
  index.publish_segment(std::move(builder).build());

  const auto results = index.search("vector search latency", {.top_k = 3});

  REQUIRE(results.size() == 2);
  REQUIRE(results[0].document_id == 1);
  REQUIRE(results[1].document_id == 3);
  REQUIRE(results[0].score > results[1].score);
}

TEST_CASE("Ingestion pipeline can publish batches into a lexical segment",
          "[pipeline][search]") {
  const auto db_path = make_test_db_path();
  std::filesystem::remove_all(db_path);

  kestral::SyntheticCorpusGenerator generator;
  kestral::LexicalSegmentBuilder builder;

  {
    kestral::DocumentStore store({.db_path = db_path.string()});
    kestral::IngestionPipeline pipeline(
        generator,
        store,
        {
            .total_documents = 100,
            .batch_size = 16,
            .consumers = {&builder},
        });

    const auto metrics = pipeline.run();
    REQUIRE(metrics.documents_written == 100);
  }

  kestral::PublishedLexicalIndex index;
  index.publish_segment(std::move(builder).build());

  REQUIRE(index.segment_count() == 1);
  REQUIRE(index.total_document_count() == 100);
  REQUIRE(index.total_unique_term_count() > 0);

  const auto results = index.search("vector search pipeline", {.top_k = 5});
  REQUIRE_FALSE(results.empty());

  std::filesystem::remove_all(db_path);
}

TEST_CASE("VByte encode and decode correctly round-trips values", "[vbyte]") {
  std::vector<std::uint32_t> test_values = {
      0, 1, 127, 128, 255, 256, 16383, 16384, 2097151, 2097152, 268435455, 268435456, 4294967295
  };

  for (const auto value : test_values) {
    std::vector<std::uint8_t> buffer;
    kestral::encode_vbyte(value, buffer);
    REQUIRE_FALSE(buffer.empty());

    std::uint32_t decoded_value = 0;
    const std::uint8_t* ptr = kestral::decode_vbyte(buffer.data(), decoded_value);
    
    REQUIRE(decoded_value == value);
    REQUIRE(ptr == buffer.data() + buffer.size());
  }
}

TEST_CASE("ConcurrentQueue push/pop round-trips correctly", "[concurrency]") {
  kestral::ConcurrentQueue<int> queue(4);

  REQUIRE(queue.push(10));
  REQUIRE(queue.push(20));
  REQUIRE(queue.push(30));

  auto v1 = queue.pop();
  auto v2 = queue.pop();
  auto v3 = queue.pop();

  REQUIRE(v1.has_value());
  REQUIRE(*v1 == 10);
  REQUIRE(v2.has_value());
  REQUIRE(*v2 == 20);
  REQUIRE(v3.has_value());
  REQUIRE(*v3 == 30);
}

TEST_CASE("ConcurrentQueue close unblocks waiting consumers", "[concurrency]") {
  kestral::ConcurrentQueue<int> queue(4);

  std::thread consumer([&queue] {
    auto result = queue.pop(); // blocks until close()
    REQUIRE_FALSE(result.has_value());
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  queue.close();
  consumer.join();
}

TEST_CASE("Parallel ingestion pipeline processes all documents",
          "[pipeline][concurrency]") {
  const auto db_path = make_test_db_path();
  std::filesystem::remove_all(db_path);

  kestral::SyntheticCorpusGenerator generator;
  {
    kestral::DocumentStore store({.db_path = db_path.string()});
    kestral::IngestionPipeline pipeline(
        generator,
        store,
        {
            .total_documents = 100,
            .batch_size = 16,
            .num_threads = 2,
        });

    const auto metrics = pipeline.run();

    REQUIRE(metrics.documents_written == 100);
    REQUIRE(metrics.batches_written > 0);
    REQUIRE(metrics.elapsed_seconds > 0.0);
    REQUIRE(metrics.documents_per_second > 0.0);
  }

  std::filesystem::remove_all(db_path);
}
