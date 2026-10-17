# OrderBook-HFT

[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20%20%2F%2023-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Benchmark](https://img.shields.io/badge/Benchmark-Google%20Benchmark-brightgreen.svg)](https://github.com/google/benchmark)
[![License](https://img.shields.io/badge/License-MIT-lightgrey.svg)](LICENSE)

A deterministic Limit Order Book (LOB) and matching engine written in modern C++20. Designed for low latency, sub-microsecond price-time priority execution, and zero runtime heap allocations on the hot path.

---

## Architectural Highlights

- **Zero Allocation Hot Path:** Memory is pre-allocated into static arena pools and PMR buffers. No `new`/`malloc` calls during order insertion, cancellation, or matching.
- **Cache-Conscious Memory Layout:** Critical queue structures are aligned to 64-byte cache lines (`alignas(64)`) to eliminate false sharing.
- **Lock-Free Concurrency:** Single-writer core fed by Single-Producer Single-Consumer (SPSC) lock-free ring buffers.
- **Price-Time Priority:** Intrusive doubly linked list per price level for O(1) order insertion, cancellation, and execution.
- **Compile-Time Checks:** C++20 concepts and `constexpr` evaluation for zero-cost abstractions.

---

## Getting Started

### Prerequisites
- Clang 16+ or GCC 13+
- CMake 3.25+
- Ninja (recommended)

### Building and Benchmarking
```bash
git clone https://github.com/StanislawGolec/OrderBook-HFT.git
cd OrderBook-HFT
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run benchmarks
./build/bin/matching_bench
```
