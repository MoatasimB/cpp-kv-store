cmake_minimum_required(VERSION 3.16)
project(kv_store CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(kv_core
    src/KVStore.cpp
    src/Wal.cpp
    src/Server.cpp
)

target_include_directories(kv_core PUBLIC include)
target_link_libraries(kv_core PUBLIC Threads::Threads)

add_executable(kv_store src/main.cpp)
target_link_libraries(kv_store PRIVATE kv_core)

add_executable(kv_tests tests/KVStoreTests.cpp)
target_link_libraries(kv_tests PRIVATE kv_core)

add_executable(kv_bench bench/kv_bench.cpp)
target_link_libraries(kv_bench PRIVATE Threads::Threads)

enable_testing()
add_test(NAME kv_tests COMMAND kv_tests)
