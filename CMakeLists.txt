cmake_minimum_required(VERSION 3.16)
project(kv_store CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(kv_core
    src/KVStore.cpp
    src/Wal.cpp
    src/Server.cpp
)

target_include_directories(kv_core PUBLIC include)
if(UNIX)
  target_link_libraries(kv_core pthread)
endif()

add_executable(kv_store src/main.cpp)
target_link_libraries(kv_store kv_core)

add_executable(kv_tests tests/KVStoreTests.cpp)
target_link_libraries(kv_tests kv_core)

add_executable(kv_bench bench/kv_bench.cpp)
target_include_directories(kv_bench PRIVATE include)
if(UNIX)
  target_link_libraries(kv_bench pthread)
endif()
