#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "orderbook/alloc_guard.hpp"
#include "orderbook/invariant_checker.hpp"
#include "orderbook/latency_profiler.hpp"
#include "orderbook/market_replay.hpp"
#include "orderbook/matching_engine.hpp"
#include "orderbook/order_book.hpp"

using namespace orderbook;

TEST_CASE("LatencyProfiler - Basic Recording and Percentiles", "[soak]") {
    LatencyProfiler profiler;
    CHECK(profiler.tsc_frequency_ghz() > 0.1);

    // Record 1000 known nanosecond samples: 1 to 1000 ns
    for (uint64_t ns = 1; ns <= 1000; ++ns) {
        profiler.record_ns(ns);
    }

    CHECK(profiler.sample_count() == 1000);
    CHECK(profiler.min_ns() >= 0.0);
    CHECK(profiler.max_ns() >= 990.0);
    CHECK(profiler.p50() >= 490.0);
    CHECK(profiler.p50() <= 510.0);
    CHECK(profiler.p90() >= 890.0);
    CHECK(profiler.p99() >= 980.0);
    CHECK(profiler.p99_9() >= 990.0);
}

TEST_CASE("LatencyProfiler - Zero Heap Allocations During Sample Recording", "[soak]") {
    LatencyProfiler profiler;

    {
        AllocationGuard guard;
        for (uint64_t i = 1; i <= 5000; ++i) {
            profiler.record_cycles(i * 10);
            profiler.record_ns(i % 1000);
        }
    }

    CHECK(profiler.sample_count() == 10000);
    SUCCEED("Recorded 10,000 latency samples with 0 dynamic heap allocations");
}

TEST_CASE("MarketReplaySimulator - Deterministic Soak Execution and Invariant Audit", "[soak]") {
    constexpr size_t NUM_OPS = 50000;
    OrderBook book(NUM_OPS * 2, 5000);
    book.preallocate(NUM_OPS * 2, 5000);
    MatchingEngine engine(book);
    LatencyProfiler profiler;

    ReplayConfig config{
        .total_operations = NUM_OPS,
        .random_seed = 0x123456789ABCDEF0ULL,
        .initial_price = Price::from_double(100.0),
        .limit_order_ratio = 0.55,
        .market_order_ratio = 0.15,
        .cancel_order_ratio = 0.15,
        .amend_order_ratio = 0.15,
        .invariant_check_interval = 10000,
    };

    MarketReplaySimulator simulator(config);
    ReplayStats stats = simulator.run(book, engine, &profiler);

    CHECK(stats.total_operations == NUM_OPS);
    CHECK(stats.limit_orders > 0);
    CHECK(stats.market_orders > 0);
    CHECK(stats.cancellations > 0);
    CHECK(stats.amendments > 0);
    CHECK(stats.trades > 0);
    CHECK(stats.total_volume_traded > 0);
    CHECK(stats.invariant_audit_passed);

    CHECK(InvariantChecker::check_no_crossed_book(book));
    auto report = InvariantChecker::check_book_consistency(book);
    CHECK(report.passed);

    CHECK(profiler.sample_count() > 0);
    CHECK(profiler.p50() > 0.0);
}

TEST_CASE("MarketReplaySimulator - Memory Pool Recycling Stability", "[soak]") {
    // Run 30,000 operations in a book capped at 5,000 capacity
    // Verifies that continuous insertions, cancels, and fills recycle memory pool slots cleanly
    constexpr size_t CAPACITY = 5000;
    constexpr size_t OPS = 30000;

    OrderBook book(CAPACITY, 500);
    book.preallocate(CAPACITY, 500);
    MatchingEngine engine(book);

    ReplayConfig config{
        .total_operations = OPS,
        .random_seed = 0xFEDCBA9876543210ULL,
        .initial_price = Price::from_double(50.0),
        .limit_order_ratio = 0.40,
        .market_order_ratio = 0.25,
        .cancel_order_ratio = 0.20,
        .amend_order_ratio = 0.15,
        .invariant_check_interval = 5000,
    };

    MarketReplaySimulator simulator(config);
    ReplayStats stats = simulator.run(book, engine);

    CHECK(stats.total_operations == OPS);
    CHECK(stats.invariant_audit_passed);
    CHECK(book.order_count() < CAPACITY);
    CHECK(InvariantChecker::check_no_crossed_book(book));
}

TEST_CASE("MarketReplaySimulator - Hot Path Zero Dynamic Memory Allocations", "[soak]") {
    constexpr size_t NUM_OPS = 20000;
    OrderBook book(NUM_OPS * 2, 2000);
    book.preallocate(NUM_OPS * 2, 2000);
    MatchingEngine engine(book);

    ReplayConfig config{
        .total_operations = NUM_OPS,
        .random_seed = 0x5555AAAA5555AAAAULL,
        .initial_price = Price::from_double(100.0),
        .limit_order_ratio = 0.50,
        .market_order_ratio = 0.20,
        .cancel_order_ratio = 0.15,
        .amend_order_ratio = 0.15,
        .invariant_check_interval = 0,  // Disable periodic check strings in alloc guard
    };

    MarketReplaySimulator simulator(config);

    {
        AllocationGuard guard;
        ReplayStats stats = simulator.run(book, engine);
        CHECK(stats.total_operations == NUM_OPS);
        CHECK(stats.trades > 0);
    }

    CHECK(InvariantChecker::check_no_crossed_book(book));
    auto report = InvariantChecker::check_book_consistency(book);
    CHECK(report.passed);

    SUCCEED("Executed 20,000 soak operations with zero dynamic heap allocations on the hot path");
}
