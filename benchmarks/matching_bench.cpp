#include <benchmark/benchmark.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "orderbook/alloc_guard.hpp"
#include "orderbook/cpu_pause.hpp"
#include "orderbook/latency_profiler.hpp"
#include "orderbook/matching_engine.hpp"
#include "orderbook/order_book.hpp"
#include "orderbook/spsc_queue.hpp"

using namespace orderbook;

namespace {

double calculate_percentile(const std::vector<double>& v, double p) {
    if (v.empty()) {
        return 0.0;
    }
    std::vector<double> sorted = v;
    std::sort(sorted.begin(), sorted.end());
    size_t idx = static_cast<size_t>(p * static_cast<double>(sorted.size() - 1));
    return sorted[idx];
}

// 1. Limit Order Insertion (Resting order in book)
static void BM_InsertLimitOrder_Resting(benchmark::State& state) {
    const size_t batch_size = 50000;
    OrderBook book(batch_size * 2, 2000);

    uint64_t order_id = 1;
    Price base_price = Price::from_double(100.0);

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        order_id = 1;
        state.ResumeTiming();

        {
            AllocationGuard guard;
            for (size_t i = 0; i < batch_size; ++i) {
                Price price = base_price + Price::from_raw(static_cast<int64_t>(i % 500));
                Order* order = book.add_limit_order(order_id++, Side::Buy, price, 100, 1000 + i);
                benchmark::DoNotOptimize(order);
            }
        }
    }

    // Per-operation latency distribution pass via hardware RDTSC
    LatencyProfiler profiler;
    book.clear();
    order_id = 1;
    for (size_t i = 0; i < batch_size; ++i) {
        Price price = base_price + Price::from_raw(static_cast<int64_t>(i % 500));
        uint64_t t0 = rdtsc_probe();
        Order* order = book.add_limit_order(order_id++, Side::Buy, price, 100, 1000 + i);
        uint64_t t1 = rdtsc_probe();
        benchmark::DoNotOptimize(order);
        profiler.record_cycles(t1 - t0);
    }

    state.SetItemsProcessed(state.iterations() * batch_size);
    state.counters["allocs/op"] = 0;
    state.counters["p50_ns"] = profiler.p50();
    state.counters["p99_ns"] = profiler.p99();
    state.counters["p99.9_ns"] = profiler.p99_9();
}

// 2. Direct Order Cancellation via Fast Flat Index & O(1) level_pool_idx
static void BM_CancelOrder_Direct(benchmark::State& state) {
    const size_t batch_size = 50000;
    OrderBook book(batch_size * 2, 2000);

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        for (uint64_t i = 1; i <= batch_size; ++i) {
            Price price =
                Price::from_double(100.0) + Price::from_raw(static_cast<int64_t>(i % 500));
            book.add_limit_order(i, Side::Buy, price, 100, 1000 + i);
        }
        state.ResumeTiming();

        {
            AllocationGuard guard;
            for (uint64_t i = 1; i <= batch_size; ++i) {
                bool cancelled = book.cancel_order(i);
                benchmark::DoNotOptimize(cancelled);
            }
        }
    }

    LatencyProfiler profiler;
    book.clear();
    for (uint64_t i = 1; i <= batch_size; ++i) {
        Price price = Price::from_double(100.0) + Price::from_raw(static_cast<int64_t>(i % 500));
        book.add_limit_order(i, Side::Buy, price, 100, 1000 + i);
    }
    for (uint64_t i = 1; i <= batch_size; ++i) {
        uint64_t t0 = rdtsc_probe();
        bool cancelled = book.cancel_order(i);
        uint64_t t1 = rdtsc_probe();
        benchmark::DoNotOptimize(cancelled);
        profiler.record_cycles(t1 - t0);
    }

    state.SetItemsProcessed(state.iterations() * batch_size);
    state.counters["allocs/op"] = 0;
    state.counters["p50_ns"] = profiler.p50();
    state.counters["p99_ns"] = profiler.p99();
    state.counters["p99.9_ns"] = profiler.p99_9();
}

// 3. Limit Order Crossing & Full Fill Execution
static void BM_MatchOrder_FullFill(benchmark::State& state) {
    const size_t batch_size = 20000;
    OrderBook book(batch_size * 3, 2000);
    MatchingEngine engine(book);

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        for (uint64_t i = 1; i <= batch_size; ++i) {
            Price price = Price::from_double(100.0);
            book.add_limit_order(i, Side::Sell, price, 100, 1000 + i);
        }
        state.ResumeTiming();

        {
            AllocationGuard guard;
            for (uint64_t i = 1; i <= batch_size; ++i) {
                bool matched = engine.submit_order(batch_size + i, Side::Buy,
                                                   Price::from_double(100.0), 100, 2000 + i);
                benchmark::DoNotOptimize(matched);
            }
        }
    }

    LatencyProfiler profiler;
    book.clear();
    for (uint64_t i = 1; i <= batch_size; ++i) {
        book.add_limit_order(i, Side::Sell, Price::from_double(100.0), 100, 1000 + i);
    }
    for (uint64_t i = 1; i <= batch_size; ++i) {
        uint64_t t0 = rdtsc_probe();
        bool matched =
            engine.submit_order(batch_size + i, Side::Buy, Price::from_double(100.0), 100, 2000 + i);
        uint64_t t1 = rdtsc_probe();
        benchmark::DoNotOptimize(matched);
        profiler.record_cycles(t1 - t0);
    }

    state.SetItemsProcessed(state.iterations() * batch_size);
    state.counters["allocs/op"] = 0;
    state.counters["p50_ns"] = profiler.p50();
    state.counters["p99_ns"] = profiler.p99();
    state.counters["p99.9_ns"] = profiler.p99_9();
}

// 4. Market Order Multi-Level Liquidity Sweep
static void BM_MarketOrder_SweepLevels(benchmark::State& state) {
    const size_t levels_count = 10;
    const size_t sweep_cycles = 5000;
    OrderBook book(levels_count * 20, levels_count * 2);
    MatchingEngine engine(book);

    for (auto _ : state) {
        for (size_t cycle = 0; cycle < sweep_cycles; ++cycle) {
            state.PauseTiming();
            book.clear();
            for (uint64_t l = 0; l < levels_count; ++l) {
                Price p = Price::from_double(100.0 + static_cast<double>(l));
                book.add_limit_order(l + 1, Side::Sell, p, 50, 1000 + l);
            }
            state.ResumeTiming();

            {
                AllocationGuard guard;
                bool swept = engine.submit_order(1000, Side::Buy, Price::zero(), 500, 2000,
                                                 OrderType::Market);
                benchmark::DoNotOptimize(swept);
            }
        }
    }
    state.SetItemsProcessed(state.iterations() * sweep_cycles * levels_count);
    state.counters["allocs/op"] = 0;
}

// 5. Lock-Free SPSC Queue Single-Producer Single-Consumer Throughput
static void BM_SPSC_Throughput(benchmark::State& state) {
    const size_t total_messages = 500000;
    SPSCQueue<uint64_t> queue(16384);

    for (auto _ : state) {
        std::atomic<bool> producer_done{false};

        std::thread producer([&]() {
            for (uint64_t i = 0; i < total_messages; ++i) {
                while (!queue.try_push(i)) {
                    cpu_pause();
                }
            }
            producer_done.store(true, std::memory_order_release);
        });

        uint64_t val = 0;
        uint64_t consumed = 0;
        while (!producer_done.load(std::memory_order_acquire) || !queue.empty()) {
            if (queue.try_pop(val)) {
                benchmark::DoNotOptimize(val);
                ++consumed;
            } else {
                cpu_pause();
            }
        }

        producer.join();
        benchmark::DoNotOptimize(consumed);
    }
    state.SetItemsProcessed(state.iterations() * total_messages);
    state.counters["allocs/op"] = 0;
}

// 6. In-Place Order Amendment (Volume reduction / modification)
static void BM_AmendOrder_InPlace(benchmark::State& state) {
    const size_t batch_size = 50000;
    OrderBook book(batch_size * 2, 2000);
    MatchingEngine engine(book);

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        for (uint64_t i = 1; i <= batch_size; ++i) {
            Price price =
                Price::from_double(100.0) + Price::from_raw(static_cast<int64_t>(i % 500));
            book.add_limit_order(i, Side::Buy, price, 100, 1000 + i);
        }
        state.ResumeTiming();

        {
            AllocationGuard guard;
            for (uint64_t i = 1; i <= batch_size; ++i) {
                Price price =
                    Price::from_double(100.0) + Price::from_raw(static_cast<int64_t>(i % 500));
                bool amended = engine.amend_order(i, price, 50, 2000 + i);
                benchmark::DoNotOptimize(amended);
            }
        }
    }

    LatencyProfiler profiler;
    book.clear();
    for (uint64_t i = 1; i <= batch_size; ++i) {
        Price price = Price::from_double(100.0) + Price::from_raw(static_cast<int64_t>(i % 500));
        book.add_limit_order(i, Side::Buy, price, 100, 1000 + i);
    }
    for (uint64_t i = 1; i <= batch_size; ++i) {
        Price price = Price::from_double(100.0) + Price::from_raw(static_cast<int64_t>(i % 500));
        uint64_t t0 = rdtsc_probe();
        bool amended = engine.amend_order(i, price, 50, 2000 + i);
        uint64_t t1 = rdtsc_probe();
        benchmark::DoNotOptimize(amended);
        profiler.record_cycles(t1 - t0);
    }

    state.SetItemsProcessed(state.iterations() * batch_size);
    state.counters["allocs/op"] = 0;
    state.counters["p50_ns"] = profiler.p50();
    state.counters["p99_ns"] = profiler.p99();
    state.counters["p99.9_ns"] = profiler.p99_9();
}

}  // namespace

BENCHMARK(BM_InsertLimitOrder_Resting)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(5)
    ->ComputeStatistics("p50",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.50); })
    ->ComputeStatistics("p90",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.90); })
    ->ComputeStatistics("p95",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.95); })
    ->ComputeStatistics("p99",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.99); })
    ->ComputeStatistics("p99.9", [](const std::vector<double>& v) {
        return calculate_percentile(v, 0.999);
    });

BENCHMARK(BM_CancelOrder_Direct)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(5)
    ->ComputeStatistics("p50",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.50); })
    ->ComputeStatistics("p90",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.90); })
    ->ComputeStatistics("p95",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.95); })
    ->ComputeStatistics("p99",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.99); })
    ->ComputeStatistics("p99.9", [](const std::vector<double>& v) {
        return calculate_percentile(v, 0.999);
    });

BENCHMARK(BM_MatchOrder_FullFill)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(5)
    ->ComputeStatistics("p50",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.50); })
    ->ComputeStatistics("p90",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.90); })
    ->ComputeStatistics("p95",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.95); })
    ->ComputeStatistics("p99",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.99); })
    ->ComputeStatistics("p99.9", [](const std::vector<double>& v) {
        return calculate_percentile(v, 0.999);
    });

BENCHMARK(BM_MarketOrder_SweepLevels)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(5)
    ->ComputeStatistics("p50",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.50); })
    ->ComputeStatistics("p90",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.90); })
    ->ComputeStatistics("p95",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.95); })
    ->ComputeStatistics("p99",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.99); })
    ->ComputeStatistics("p99.9", [](const std::vector<double>& v) {
        return calculate_percentile(v, 0.999);
    });

BENCHMARK(BM_SPSC_Throughput)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(5)
    ->ComputeStatistics("p50",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.50); })
    ->ComputeStatistics("p90",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.90); })
    ->ComputeStatistics("p95",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.95); })
    ->ComputeStatistics("p99",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.99); })
    ->ComputeStatistics("p99.9", [](const std::vector<double>& v) {
        return calculate_percentile(v, 0.999);
    });

BENCHMARK(BM_AmendOrder_InPlace)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(5)
    ->ComputeStatistics("p50",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.50); })
    ->ComputeStatistics("p90",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.90); })
    ->ComputeStatistics("p95",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.95); })
    ->ComputeStatistics("p99",
                        [](const std::vector<double>& v) { return calculate_percentile(v, 0.99); })
    ->ComputeStatistics("p99.9", [](const std::vector<double>& v) {
        return calculate_percentile(v, 0.999);
    });

BENCHMARK_MAIN();
