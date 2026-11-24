cmake_minimum_required(VERSION 3.16)
project(AtlasRisk CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -Wall -Wextra")

include_directories(include)

add_library(atlas_engine src/order_book.cpp src/order_book_fast.cpp)

add_executable(atlas_tests tests/test_order_book.cpp)
target_link_libraries(atlas_tests atlas_engine)

add_executable(atlas_diff tests/test_differential.cpp)
target_link_libraries(atlas_diff atlas_engine)

add_executable(atlas_bench bench/benchmark.cpp)
target_link_libraries(atlas_bench atlas_engine)

add_executable(atlas_compare bench/benchmark_compare.cpp)
target_link_libraries(atlas_compare atlas_engine)

add_executable(atlas_replay bench/market_replay.cpp)
target_link_libraries(atlas_replay atlas_engine)

enable_testing()
add_test(NAME unit_tests COMMAND atlas_tests)
add_test(NAME differential COMMAND atlas_diff)
