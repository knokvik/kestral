set(KESTRAL_LIBRARY_SOURCES
    ingest/ingestion_pipeline.cpp
    ingest/synthetic_corpus.cpp
    search/lexical_index.cpp
    search/tokenizer.cpp
    search/vbyte.cpp
    storage/document_store.cpp
)

add_library(kestral STATIC ${KESTRAL_LIBRARY_SOURCES})
target_include_directories(kestral PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(kestral PUBLIC cxx_std_20)
target_link_libraries(kestral PUBLIC RocksDB::rocksdb spdlog::spdlog)

add_executable(benchmark_exe benchmark.cpp)
target_link_libraries(benchmark_exe PRIVATE kestral benchmark::benchmark_main)
