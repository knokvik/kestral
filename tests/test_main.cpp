#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "kestral/core/concurrent_queue.hpp"
#include "kestral/ingest/ingestion_pipeline.hpp"
#include "kestral/ingest/synthetic_corpus.hpp"
#include "kestral/search/hybrid_search.hpp"
#include "kestral/search/lexical_index.hpp"
#include "kestral/search/tokenizer.hpp"
#include "kestral/search/vbyte.hpp"
#include "kestral/search/vector_index.hpp"
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

TEST_CASE("VectorIndex integrates with USearch correctly", "[search]") {
  kestral::VectorIndex index(128);
  REQUIRE(index.dimensions() == 128);
  REQUIRE(index.size() == 0);

  kestral::DocumentBatch batch;
  std::vector<float> v1(128, 0.1f);
  std::vector<float> v2(128, 0.0f); v2[0] = 1.0f;
  std::vector<float> v3(128, -0.1f); v3[0] = 1.0f;

  batch.add({.id = 1, .embedding = v1});
  batch.add({.id = 2, .embedding = v2});
  batch.add({.id = 3, .embedding = v3});
  
  index.consume(batch.documents());
  REQUIRE(index.size() == 3);

  std::vector<float> query_v(128, 0.0f); query_v[0] = 1.0f;
  auto results = index.search(query_v, 10);
  REQUIRE( results.size() == 3 );
  // v2 matches best (cosine distance to itself is 0, others are >0)
  REQUIRE( results[0].document_id == 2 );
  REQUIRE( results[0].distance == Catch::Approx(0.0f).margin(1e-5f) );
}

TEST_CASE("HybridSearchEngine fuses lexical and vector scores correctly", "[hybrid]") {
  kestral::LexicalSegmentBuilder lexical_builder;
  kestral::VectorIndex vector_index(128);

  kestral::DocumentBatch batch;
  std::vector<float> v1(128, 0.1f);
  std::vector<float> v2(128, 0.0f); v2[0] = 1.0f;
  
  // Document 1 has a strong lexical match ("hybrid engine") but weak vector
  batch.add({.id = 1, .title = "Hybrid Engine", .body = "Lexical search is fast", .embedding = v1});
  
  // Document 2 has a weak lexical match but strong vector match
  batch.add({.id = 2, .title = "Another document", .body = "Nothing interesting here", .embedding = v2});
  
  // Document 3 has both lexical match and vector match (should win RRF)
  batch.add({.id = 3, .title = "Hybrid Engine", .body = "Vector search is fast", .embedding = v2});

  lexical_builder.consume(batch.documents());
  vector_index.consume(batch.documents());

  kestral::PublishedLexicalIndex lexical_index;
  lexical_index.publish_segment(std::move(lexical_builder).build());

  kestral::HybridSearchEngine hybrid_engine(lexical_index, vector_index);

  auto results = hybrid_engine.search("hybrid engine", v2, 10);
  REQUIRE(results.size() == 3);
  
  // Doc 3 should be rank 1 (strong lexical + strong vector)
  REQUIRE(results[0].document_id == 3);
  
  // Scores should be > 0 and descending
  REQUIRE(results[0].score > 0.0f);
  REQUIRE(results[0].score >= results[1].score);
  REQUIRE(results[1].score >= results[2].score);
}

TEST_CASE("Skip Pointers and DAAT intersection", "[search]") {
  kestral::LexicalSegmentBuilder builder;
  kestral::DocumentBatch batch;

  // We want to force a skip block. kSkipInterval is 128.
  // We'll create 300 documents.
  // Term "common" is in all documents.
  // Term "rare" is in doc ID 250 only.
  
  for (std::uint32_t i = 1; i <= 300; ++i) {
    std::string text = "common";
    if (i == 250) {
      text += " rare";
    }
    batch.add({.id = i, .title = "Test", .body = std::move(text), .embedding = {}});
  }

  builder.consume(batch.documents());
  kestral::PublishedLexicalIndex index;
  index.publish_segment(std::move(builder).build());

  // TAAT OR query
  auto results_or = index.search("common rare", {.top_k = 10, .require_all_terms = false});
  REQUIRE(results_or.size() == 10); // Returns top 10 docs that have "common"
  REQUIRE(results_or[0].document_id == 250); // "rare" gives extra score

  // DAAT AND query
  auto results_and = index.search("common rare", {.top_k = 10, .require_all_terms = true});
  REQUIRE(results_and.size() == 1); // Only doc 250 has both
  REQUIRE(results_and[0].document_id == 250);
}

#include "kestral/search/query_cache.hpp"
#include <thread>

TEST_CASE("QueryCache LRU and TTL", "[cache]") {
  kestral::QueryCache cache(2, std::chrono::milliseconds(50));

  kestral::CacheKey key1{"query1", 10, false};
  kestral::CacheKey key2{"query2", 10, false};
  kestral::CacheKey key3{"query3", 10, false};

  std::vector<kestral::SearchResult> res1{{1, 1.0, 1}};
  std::vector<kestral::SearchResult> res2{{2, 1.0, 1}};
  std::vector<kestral::SearchResult> res3{{3, 1.0, 1}};

  cache.put(key1, res1);
  cache.put(key2, res2);

  REQUIRE(cache.size() == 2);
  REQUIRE(cache.get(key1).has_value());
  REQUIRE(cache.get(key2).has_value());

  // Insert 3rd, should evict key1 (since we didn't update LRU on read)
  cache.put(key3, res3);
  REQUIRE(cache.size() == 2);
  REQUIRE(!cache.get(key1).has_value()); // Evicted
  REQUIRE(cache.get(key2).has_value());
  REQUIRE(cache.get(key3).has_value());

  // Test TTL
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  REQUIRE(!cache.get(key2).has_value()); // Expired
}

#include "kestral/search/deleted_docs.hpp"
#include "kestral/search/hybrid_search.hpp"

TEST_CASE("Soft Deletes", "[search]") {
  kestral::LexicalSegmentBuilder builder;
  kestral::DocumentBatch batch;
  kestral::VectorIndex vector_index(128);

  for (std::uint32_t i = 1; i <= 5; ++i) {
    batch.add({.id = i, .title = "Doc", .body = "hello world", .embedding = std::vector<float>(128, 0.1f)});
  }

  builder.consume(batch.documents());
  vector_index.consume(batch.documents());
  kestral::PublishedLexicalIndex lexical_index;
  lexical_index.publish_segment(std::move(builder).build());

  auto deleted_docs = std::make_shared<kestral::DeletedDocs>();
  kestral::HybridSearchEngine engine(lexical_index, vector_index, nullptr, deleted_docs);

  std::vector<float> query_vec(128, 0.1f);

  auto results_before = engine.search("hello", query_vec, 10);
  REQUIRE(results_before.size() == 5);

  deleted_docs->mark_deleted(2);
  deleted_docs->mark_deleted(4);

  auto results_after = engine.search("hello", query_vec, 10);
  REQUIRE(results_after.size() == 3);

  // Check that IDs 2 and 4 are not in the results
  for (const auto& res : results_after) {
    REQUIRE(res.document_id != 2);
    REQUIRE(res.document_id != 4);
  }
}
