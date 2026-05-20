#include <iostream>
#include <string>
#include <spdlog/spdlog.h>
#include "my_library.hpp"

int main() {
    spdlog::set_level(spdlog::level::info);

    // Generate 10M documents
    DocumentStore store;
    store.reserve(10000000);  // Reserve space for 10M documents
    SyntheticDataGenerator generator;
    generator.generate(store, 10000000);

    // Write to RocksDB
    DocumentDB db("./document_db");
    for (size_t i = 0; i < store.ids.size(); ++i) {
        std::string doc_data = std::to_string(store.timestamps[i]) + "|" +
                               store.titles[i] + "|" + store.contents[i];
        db.write_document(store.ids[i], doc_data);
    }

    // Read a document
    std::string retrieved_data = db.read_document(1);
    spdlog::info("Retrieved document 1: {}", retrieved_data);

    return 0;
}
