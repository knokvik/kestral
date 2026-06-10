#include "kestral/storage/document_store.hpp"

#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/write_batch.h>
#include <spdlog/spdlog.h>

#include <stdexcept>
#include <utility>

namespace kestral {

DocumentStore::DocumentStore(DocumentStoreOptions options)
    : options_(std::move(options)) {
  rocksdb::Options db_options;
  db_options.create_if_missing = options_.create_if_missing;
  db_options.IncreaseParallelism(options_.max_background_jobs);
  db_options.OptimizeLevelStyleCompaction();
  db_options.write_buffer_size = options_.write_buffer_size_mb * 1024 * 1024;

  const auto status = rocksdb::DB::Open(db_options, options_.db_path, &db_);
  if (!status.ok()) {
    throw std::runtime_error("Failed to open RocksDB at " + options_.db_path +
                             ": " + status.ToString());
  }
}

DocumentStore::~DocumentStore() = default;

void DocumentStore::write_batch(std::span<const Document> documents) {
  if (documents.empty()) {
    return;
  }

  rocksdb::WriteBatch write_batch;
  for (const auto &document : documents) {
    const auto key = std::to_string(document.id);
    const auto payload = serialize_document(document);
    write_batch.Put(key, payload);
  }

  const auto status = db_->Write(build_write_options(), &write_batch);
  if (!status.ok()) {
    throw std::runtime_error("Failed to write document batch: " +
                             status.ToString());
  }
}

std::optional<Document> DocumentStore::read_document(std::uint64_t id) const {
  std::string payload;
  const auto status =
      db_->Get(rocksdb::ReadOptions(), std::to_string(id), &payload);

  if (status.IsNotFound()) {
    return std::nullopt;
  }

  if (!status.ok()) {
    throw std::runtime_error("Failed to read document " + std::to_string(id) +
                             ": " + status.ToString());
  }

  return deserialize_document(id, payload);
}

void DocumentStore::flush() {
  rocksdb::FlushOptions flush_options;
  flush_options.wait = true;

  const auto status = db_->Flush(flush_options);
  if (!status.ok()) {
    throw std::runtime_error("Failed to flush RocksDB memtables: " +
                             status.ToString());
  }
}

rocksdb::WriteOptions DocumentStore::build_write_options() const {
  rocksdb::WriteOptions write_options;
  write_options.disableWAL = options_.disable_wal;
  write_options.sync = false;
  return write_options;
}

std::string DocumentStore::serialize_document(const Document &document) const {
  std::string payload;
  payload.reserve(document.title.size() + document.body.size() + 32);
  payload.append(std::to_string(document.timestamp));
  payload.push_back('\n');
  payload.append(document.title);
  payload.push_back('\n');
  payload.append(document.body);
  return payload;
}

Document DocumentStore::deserialize_document(std::uint64_t id,
                                             std::string_view payload) const {
  const auto first_break = payload.find('\n');
  const auto second_break = payload.find('\n', first_break + 1);

  if (first_break == std::string_view::npos ||
      second_break == std::string_view::npos) {
    throw std::runtime_error("Corrupt document payload for document " +
                             std::to_string(id));
  }

  Document document;
  document.id = id;
  document.timestamp =
      std::stoull(std::string(payload.substr(0, first_break)));
  document.title = std::string(
      payload.substr(first_break + 1, second_break - first_break - 1));
  document.body = std::string(payload.substr(second_break + 1));
  return document;
}

} // namespace kestral
