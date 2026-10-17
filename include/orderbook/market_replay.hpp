#pragma once

#include <cstddef>
#include <cstdint>

#include "orderbook/latency_profiler.hpp"
#include "orderbook/matching_engine.hpp"
#include "orderbook/order_book.hpp"
#include "orderbook/price.hpp"
#include "orderbook/types.hpp"

namespace orderbook {

struct ReplayConfig {
    size_t total_operations{1000000};
    uint64_t random_seed{0xCAFEBABEDEADBEEFULL};
    Price initial_price{Price::from_double(100.0)};
    double limit_order_ratio{0.60};          // 60% resting limit orders
    double market_order_ratio{0.15};         // 15% aggressive sweeps
    double cancel_order_ratio{0.15};         // 15% cancellations
    double amend_order_ratio{0.10};          // 10% in-place amendments
    size_t invariant_check_interval{50000};  // Periodic invariant audit
};

struct ReplayStats {
    size_t total_operations{0};
    size_t limit_orders{0};
    size_t market_orders{0};
    size_t cancellations{0};
    size_t amendments{0};
    size_t trades{0};
    Volume total_volume_traded{0};
    bool invariant_audit_passed{true};
    double elapsed_seconds{0.0};
    double ops_per_second{0.0};
};

// High-Throughput Synthetic Market Replay Engine and Soak Test Harness
class MarketReplaySimulator {
public:
    explicit MarketReplaySimulator(const ReplayConfig& config = ReplayConfig{});

    // Executes the full soak test run with latency profiling
    ReplayStats run(OrderBook& book, MatchingEngine& engine, LatencyProfiler* profiler = nullptr);

    [[nodiscard]] const ReplayConfig& config() const noexcept { return config_; }

private:
    ReplayConfig config_;
    std::vector<OrderId> active_order_ids_{};
};

}  // namespace orderbook
