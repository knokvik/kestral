#include "my_library.hpp"
#include <chrono>
#include <iostream>
#include <spdlog/spdlog.h>

void add_document(DocumentStore &store, uint64_t id, uint64_t timestamp,
                  std::string title, std::string content) {
  store.ids.push_back(id);
  store.timestamps.push_back(timestamp);
  store.titles.push_back(std::move(title));
  store.contents.push_back(std::move(content));
}

SyntheticDataGenerator::SyntheticDataGenerator() {
  gen.seed(std::chrono::system_clock::now().time_since_epoch().count());
  words = {"lorem", "ipsum",       "dolor",      "sit",
           "amet",  "consectetur", "adipiscing", "elit"};
  categories = {"technical", "science", "business", "health", "entertainment"};
}

void SyntheticDataGenerator::generate(DocumentStore &store, size_t num_docs) {
  for (size_t i = 0; i < num_docs; ++i) {
    uint64_t id = i + 1;
    uint64_t timestamp = 1609459200 + (i % 1000000);
    add_document(store, id, timestamp, generate_title(), generate_content());
  }
}

std::string SyntheticDataGenerator::generate_title() {
  std::string title;
  for (int i = 0; i < 5; ++i) {
    title += words[gen() % words.size()] + " ";
  }
  return title;
}

std::string SyntheticDataGenerator::generate_content() {
  std::string content;
  for (int i = 0; i < 100; ++i) {
    content += words[gen() % words.size()] + " ";
  }
  return content;
}

DocumentDB::DocumentDB(std::string_view db_path) {
  rocksdb::Options options;
  options.create_if_missing = true;
  rocksdb::Status status =
      rocksdb::DB::Open(options, std::string(db_path), &db);
  if (!status.ok()) {
    spdlog::error("Failed to open RocksDB: {}", status.ToString());
    throw std::runtime_error(status.ToString());
  }
  spdlog::info("RocksDB opened successfully at {}", db_path);
}

DocumentDB::~DocumentDB() {
  if (db) {
    db->Close();
    spdlog::info("RocksDB closed.");
  }
}

void DocumentDB::write_document(uint64_t id, std::string_view doc_data) {
  if (db) {
    rocksdb::Status status =
        db->Put(rocksdb::WriteOptions(), std::to_string(id), doc_data);
    if (!status.ok()) {
      spdlog::error("Failed to write document {}: {}", id, status.ToString());
    }
  }
}

std::string DocumentDB::read_document(uint64_t id) {
  std::string value;
  if (db) {
    rocksdb::Status status =
        db->Get(rocksdb::ReadOptions(), std::to_string(id), &value);
    if (!status.ok()) {
      spdlog::error("Failed to read document {}: {}", id, status.ToString());
      return "";
    }
  }
  return value;
}
