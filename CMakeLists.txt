cmake_minimum_required(VERSION 3.25)
project(kestral LANGUAGES CXX)

set(CMAKE_TOOLCHAIN_FILE
"${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE
 STRING "")

 add_subdirectory(src)