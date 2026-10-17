#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <vector>

#include "orderbook/cpu_affinity.hpp"
#include "orderbook/cpu_pause.hpp"
#include "orderbook/engine_service.hpp"
#include "orderbook/matching_engine.hpp"

using namespace orderbook;

namespace {

class ThreadSafeListener : public IMatchingListener {
public:
    std::atomic<size_t> trade_count{0};
    std::atomic<size_t> accept_count{0};
    std::atomic<size_t> cancel_count{0};

    void on_order_accepted(const OrderAccepted&) noexcept override {
        trade_count.fetch_add(0, std::memory_order_relaxed);
        accept_count.fetch_add(1, std::memory_order_relaxed);
    }

    void on_order_cancelled(const OrderCancelled&) noexcept override {
        cancel_count.fetch_add(1, std::memory_order_relaxed);
    }

    void on_trade(const TradeEvent&) noexcept override {
        trade_count.fetch_add(1, std::memory_order_relaxed);
    }
};

}  // namespace

TEST_CASE("CPU Affinity Utilities", "[cpu_affinity]") {
    uint32_t cores = get_hardware_concurrency();
    REQUIRE(cores > 0);

    // Attempting to pin to core 0 (should succeed on modern systems)
    bool pinned = pin_current_thread_to_core(0);
    // On some restricted CI environments pinning might return false, but it must not crash!
    (void)pinned;

    bool priority_set = set_thread_high_priority();
    (void)priority_set;
}

TEST_CASE("EngineService - Asynchronous Order Ingestion and Execution", "[engine_service]") {
    OrderBook book(4096, 512);
    ThreadSafeListener listener;
    MatchingEngine engine(book, &listener);

    EngineService service(engine, 1024, std::nullopt);
    REQUIRE(!service.is_running());

    service.start();
    REQUIRE(service.is_running());

    // Submit resting ask
    REQUIRE(service.submit_order(1, Side::Sell, Price::from_double(100.0), 50, 1000));

    // Wait until processed
    while (service.processed_commands() < 1) {
        cpu_pause();
    }

    // Submit crossing bid
    REQUIRE(service.submit_order(2, Side::Buy, Price::from_double(100.0), 30, 2000));

    while (service.processed_commands() < 2) {
        cpu_pause();
    }

    // Submit cancel for remaining ask
    REQUIRE(service.cancel_order(1, 3000));

    while (service.processed_commands() < 3) {
        cpu_pause();
    }

    service.stop();
    service.join();

    REQUIRE(!service.is_running());
    REQUIRE(listener.accept_count.load() == 2);
    REQUIRE(listener.trade_count.load() == 1);
    REQUIRE(listener.cancel_count.load() == 1);
    REQUIRE(book.is_empty());
}
