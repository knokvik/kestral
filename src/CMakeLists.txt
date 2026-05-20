# 1. Build the library
add_library(my_library STATIC my_library.cpp)

# Point to the include folder one level up (in the root folder)
target_include_directories(my_library PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_compile_features(my_library PUBLIC cxx_std_20)
target_link_libraries(my_library PRIVATE RocksDB::rocksdb spdlog::spdlog) 

# 2. Build the benchmark executable
add_executable(benchmark_exe benchmark.cpp)

# Give benchmark_exe access to the root include folder as well
target_include_directories(benchmark_exe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(benchmark_exe PRIVATE my_library benchmark::benchmark_main)
