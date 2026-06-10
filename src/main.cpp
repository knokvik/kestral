#include "kestral/ingest/ingestion_pipeline.hpp"
#include "kestral/ingest/synthetic_corpus.hpp"
#include "kestral/search/lexical_index.hpp"
#include "kestral/storage/document_store.hpp"
#include <filesystem>
#include <spdlog/spdlog.h>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct CommandLineOptions {
  std::string db_path = "./document_db";
  std::size_t document_count = 100000;
  std::size_t batch_size = 4096;
  std::size_t write_buffer_size_mb = 128;
  std::size_t top_k = 5;
  std::string query;
};

std::uint64_t parse_unsigned(std::string_view value, std::string_view flag_name) {
  std::size_t parsed_characters = 0;
  const auto parsed_value =
      std::stoull(std::string(value), &parsed_characters, 10);
  if (parsed_characters != value.size()) {
    throw std::invalid_argument("Invalid numeric value for " +
                                std::string(flag_name));
  }

  return parsed_value;
}

CommandLineOptions parse_command_line(int argc, char **argv) {
  CommandLineOptions options;

  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--docs" && index + 1 < argc) {
      options.document_count = parse_unsigned(argv[++index], "--docs");
      continue;
    }

    if (argument == "--batch-size" && index + 1 < argc) {
      options.batch_size = parse_unsigned(argv[++index], "--batch-size");
      continue;
    }

    if (argument == "--db-path" && index + 1 < argc) {
      options.db_path = argv[++index];
      continue;
    }

    if (argument == "--write-buffer-mb" && index + 1 < argc) {
      options.write_buffer_size_mb =
          parse_unsigned(argv[++index], "--write-buffer-mb");
      continue;
    }

    if (argument == "--top-k" && index + 1 < argc) {
      options.top_k = parse_unsigned(argv[++index], "--top-k");
      continue;
    }

    if (argument == "--query" && index + 1 < argc) {
      options.query = argv[++index];
      continue;
    }

    throw std::invalid_argument("Unknown or incomplete argument: " +
                                std::string(argument));
  }

  return options;
}

} // namespace

int main(int argc, char **argv) {
  spdlog::set_level(spdlog::level::info);

  try {
    const auto options = parse_command_line(argc, argv);
    std::filesystem::remove_all(options.db_path);

    kestral::DocumentStore store({
        .db_path = options.db_path,
        .write_buffer_size_mb = options.write_buffer_size_mb,
    });

    kestral::SyntheticCorpusGenerator generator;
    kestral::LexicalSegmentBuilder lexical_segment_builder;
    kestral::IngestionPipeline pipeline(
        generator,
        store,
        {
            .total_documents = options.document_count,
            .batch_size = options.batch_size,
            .consumers = {&lexical_segment_builder},
        });

    const auto metrics = pipeline.run();
    kestral::PublishedLexicalIndex lexical_index;
    lexical_index.publish_segment(std::move(lexical_segment_builder).build());

    spdlog::info("Ingested {} documents in {:.3f}s ({:.0f} docs/sec)",
                 metrics.documents_written,
                 metrics.elapsed_seconds,
                 metrics.documents_per_second);
    spdlog::info("Average batch latency: {:.3f} ms | p95: {:.3f} ms | max: {:.3f} ms",
                 metrics.average_batch_latency_ms,
                 metrics.p95_batch_latency_ms,
                 metrics.max_batch_latency_ms);
    spdlog::info("Published {} lexical segment covering {} documents and {} unique terms",
                 lexical_index.segment_count(),
                 lexical_index.total_document_count(),
                 lexical_index.total_unique_term_count());

    if (const auto first_document = store.read_document(1)) {
      spdlog::info("Document 1 title: {}", first_document->title);
    }

    if (!options.query.empty()) {
      const auto results = lexical_index.search(
          options.query, {.top_k = options.top_k});
      spdlog::info("Lexical search for '{}' returned {} result(s)",
                   options.query,
                   results.size());

      for (const auto &result : results) {
        spdlog::info("doc={} score={:.4f} matched_terms={}",
                     result.document_id,
                     result.score,
                     result.matched_terms);
      }
    }
  } catch (const std::exception &error) {
    spdlog::error("kestral_run failed: {}", error.what());
    return 1;
  }

  return 0;
}
