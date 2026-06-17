cmake_minimum_required(VERSION 3.25)

# 1. Toolchain file must go BEFORE project()
set(CMAKE_TOOLCHAIN_FILE
"${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE
 STRING "")

project(kestral LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 2. Apple Silicon OpenMP Overrides
if(APPLE)
    set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include")
    set(OpenMP_CXX_LIB_NAMES "omp")
    set(OpenMP_omp_LIBRARY "/opt/homebrew/opt/libomp/lib/libomp.dylib")
endif()

# 3. Find Packages
find_package(OpenMP REQUIRED)
find_package(Catch2 CONFIG REQUIRED)
find_package(RocksDB CONFIG REQUIRED)
find_package(benchmark CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(usearch CONFIG REQUIRED)

# 4. Process the src subdirectory (this builds the core library and benchmarks)
add_subdirectory(src)

# 5. Build core executable (placed in root context since it references ./src/main.cpp)
add_executable(kestral_run ./src/main.cpp)
target_link_libraries(kestral_run PRIVATE 
    kestral 
    OpenMP::OpenMP_CXX 
    RocksDB::rocksdb
    spdlog::spdlog
)

# 6. Testing Setup
enable_testing()
add_executable(test_main tests/test_main.cpp)
target_link_libraries(test_main PRIVATE kestral Catch2::Catch2WithMain)
add_test(NAME test_main COMMAND test_main)
