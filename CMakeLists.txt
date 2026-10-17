cmake_minimum_required(VERSION 3.25)
project(OrderBookHFT VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ORDERBOOK_BUILD_TESTS "Build test suite" ON)
option(ORDERBOOK_BUILD_BENCHMARKS "Build benchmarks" ON)
option(ORDERBOOK_ENABLE_SANITIZERS "Enable ASan/UBSan sanitizers" OFF)

# Warning and compiler optimization profile
add_library(orderbook_flags INTERFACE)

if(MSVC)
    target_compile_options(orderbook_flags INTERFACE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /volatile:iso
        /EHsc
        /utf-8
        $<$<CONFIG:Release>:/O2 /Oi /Gy /GR->
    )
    if(ORDERBOOK_ENABLE_SANITIZERS)
        target_compile_options(orderbook_flags INTERFACE /fsanitize=address)
    endif()
else()
    target_compile_options(orderbook_flags INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Werror
        $<$<CONFIG:Release>:-O3 -march=native -mtune=native -DNDEBUG -flto -fno-rtti>
        $<$<CONFIG:Debug>:-g -O0>
    )
    if(ORDERBOOK_ENABLE_SANITIZERS)
        target_compile_options(orderbook_flags INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(orderbook_flags INTERFACE -fsanitize=address,undefined)
    endif()
endif()

# Core order book static library
add_library(orderbook_core STATIC
    src/alloc_guard.cpp
    src/order_book_side.cpp
    src/order_book.cpp
    src/matching_engine.cpp
    src/cpu_affinity.cpp
    src/engine_service.cpp
    src/invariant_checker.cpp
    src/market_data_publisher.cpp
    src/feed_handler.cpp
    src/symbol_router.cpp
    src/market_replay.cpp
)

target_include_directories(orderbook_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(orderbook_core PUBLIC cxx_std_20)
target_link_libraries(orderbook_core PUBLIC orderbook_flags)

# High-throughput standalone soak and replay tool
add_executable(market_replay tools/market_replay.cpp)
target_link_libraries(market_replay PRIVATE orderbook_core)

# Testing configuration
if(ORDERBOOK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Benchmarking configuration
if(ORDERBOOK_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

