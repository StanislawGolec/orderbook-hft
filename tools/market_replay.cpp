#include "orderbook/market_replay.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>

#include "orderbook/alloc_guard.hpp"
#include "orderbook/latency_profiler.hpp"
#include "orderbook/matching_engine.hpp"
#include "orderbook/order_book.hpp"

using namespace orderbook;

int main(int argc, char* argv[]) {
    size_t total_ops = 1000000;
    uint64_t seed = 0xCAFEBABEDEADBEEFULL;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--ops" && i + 1 < argc) {
            total_ops = static_cast<size_t>(std::strtoull(argv[++i], nullptr, 10));
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = std::strtoull(argv[++i], nullptr, 10);
        }
    }

    std::cout << "=================================================================\n";
    std::cout << " OrderBook-HFT: High-Throughput Synthetic Market Replay Harness\n";
    std::cout << "=================================================================\n";
    std::cout << " Operations:       " << total_ops << "\n";
    std::cout << " Random Seed:      0x" << std::hex << seed << std::dec << "\n";

    OrderBook book(total_ops * 2, 50000);
    book.preallocate(total_ops * 2, 50000);
    MatchingEngine engine(book);

    LatencyProfiler profiler;
    std::cout << " Calibrated TSC:   " << std::fixed << std::setprecision(3)
              << profiler.tsc_frequency_ghz() << " GHz\n";
    std::cout << " Starting soak simulation...\n\n";

    ReplayConfig config{
        .total_operations = total_ops,
        .random_seed = seed,
        .initial_price = Price::from_double(100.0),
        .limit_order_ratio = 0.60,
        .market_order_ratio = 0.15,
        .cancel_order_ratio = 0.15,
        .amend_order_ratio = 0.10,
        .invariant_check_interval = total_ops / 10,
    };

    MarketReplaySimulator simulator(config);
    ReplayStats stats = simulator.run(book, engine, &profiler);

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << " Execution & Throughput Results:\n";
    std::cout << "-----------------------------------------------------------------\n";
    std::cout << " Elapsed Time:     " << std::setprecision(3) << stats.elapsed_seconds << " s\n";
    std::cout << " Throughput:       " << std::setprecision(2) << (stats.ops_per_second / 1e6)
              << " M ops/sec\n";
    std::cout << " Limit Orders:     " << stats.limit_orders << "\n";
    std::cout << " Market Orders:    " << stats.market_orders << "\n";
    std::cout << " Cancellations:    " << stats.cancellations << "\n";
    std::cout << " Amendments:       " << stats.amendments << "\n";
    std::cout << " Trades Executed:  " << stats.trades << "\n";
    std::cout << " Traded Volume:    " << stats.total_volume_traded << "\n";
    std::cout << " Invariance Audit: "
              << (stats.invariant_audit_passed ? "PASSED (No Crossed Book, Consistent Lists)"
                                               : "FAILED")
              << "\n";

    std::cout << "\n-----------------------------------------------------------------\n";
    std::cout << " Latency Percentile Distribution (Hardware RDTSC):\n";
    std::cout << "-----------------------------------------------------------------\n";
    std::cout << " Min Latency:      " << std::setprecision(1) << profiler.min_ns() << " ns\n";
    std::cout << " Mean Latency:     " << std::setprecision(1) << profiler.mean_ns() << " ns\n";
    std::cout << " p50 (Median):     " << std::setprecision(1) << profiler.p50() << " ns\n";
    std::cout << " p90:              " << std::setprecision(1) << profiler.p90() << " ns\n";
    std::cout << " p95:              " << std::setprecision(1) << profiler.p95() << " ns\n";
    std::cout << " p99:              " << std::setprecision(1) << profiler.p99() << " ns\n";
    std::cout << " p99.9:            " << std::setprecision(1) << profiler.p99_9() << " ns\n";
    std::cout << " p99.99:           " << std::setprecision(1) << profiler.p99_99() << " ns\n";
    std::cout << " Max Jitter Spike: " << std::setprecision(1) << profiler.max_ns() << " ns\n";
    std::cout << "=================================================================\n";

    return stats.invariant_audit_passed ? 0 : 1;
}
