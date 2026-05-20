#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <random>
#include <memory>
#include <string_view>
#include <span>
#include <rocksdb/db.h>

// 1. Structure of Arrays Data Layout
struct DocumentStore {
    std::vector<uint64_t> ids;
    std::vector<uint64_t> timestamps;
    std::vector<std::string> titles;
    std::vector<std::string> contents;

    void reserve(size_t size) {
        ids.reserve(size);
        timestamps.reserve(size);
        titles.reserve(size);
        contents.reserve(size);
    }
};

// Helper function declaration
void add_document(DocumentStore& store, uint64_t id, uint64_t timestamp,
                  std::string title, std::string content);

// 2. Synthetic Data Generator
class SyntheticDataGenerator {
public:
    SyntheticDataGenerator();
    void generate(DocumentStore& store, size_t num_docs);

private:
    std::mt19937_64 gen;
    std::vector<std::string> words;
    std::vector<std::string> categories;
    std::string generate_title();
    std::string generate_content();
};

// 3. Document Database (Wrapper for RocksDB)
class DocumentDB {
public:
    explicit DocumentDB(std::string_view db_path);
    ~DocumentDB();
    void write_document(uint64_t id, std::string_view doc_data);
    std::string read_document(uint64_t id);

private:
    std::unique_ptr<rocksdb::DB> db;
};
