#include <benchmark/benchmark.h>
#include <iostream>
#include "my_library.hpp"
// Benchmark document generation
static void BM_GenerateDocuments(benchmark::State& state) {
    DocumentStore store;
    SyntheticDataGenerator generator;
    for (auto _ : state) {
        generator.generate(store, 1000);  // Generate 1000 documents per iteration
    }
}
BENCHMARK(BM_GenerateDocuments);

// Benchmark RocksDB writes
static void BM_RocksDBWrites(benchmark::State& state) {
    DocumentDB db("./benchmark_db");
    DocumentStore store;
    SyntheticDataGenerator generator;
    generator.generate(store, 1000);  // Pre-generate 1000 documents

    for (auto _ : state) {
        for (size_t i = 0; i < store.ids.size(); ++i) {
            std::string doc_data = std::to_string(store.timestamps[i]) + "|" + store.titles[i] + "|" + store.contents[i];
            db.write_document(store.ids[i], doc_data);
        }
    }
}
BENCHMARK(BM_RocksDBWrites);

BENCHMARK_MAIN();