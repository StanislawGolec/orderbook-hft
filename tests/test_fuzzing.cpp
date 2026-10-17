#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <random>
#include <vector>

#include "orderbook/alloc_guard.hpp"
#include "orderbook/invariant_checker.hpp"
#include "orderbook/matching_engine.hpp"

using namespace orderbook;

namespace {

class FuzzTrackingListener : public IMatchingListener {
public:
    uint64_t total_trades_count{0};
    Volume total_traded_volume{0};
    Volume total_cancelled_volume{0};
    Volume total_rejected_volume{0};
    size_t rejected_count{0};

    void on_order_rejected(const OrderRejected&) noexcept override { ++rejected_count; }

    void on_order_cancelled(const OrderCancelled& event) noexcept override {
        total_cancelled_volume += event.remaining_quantity;
    }

    void on_trade(const TradeEvent& event) noexcept override {
        ++total_trades_count;
        total_traded_volume += event.quantity;
    }

    void reset() noexcept {
        total_trades_count = 0;
        total_traded_volume = 0;
        total_cancelled_volume = 0;
        total_rejected_volume = 0;
        rejected_count = 0;
    }
};

}  // namespace

TEST_CASE("Fuzzing - 100,000 Randomized Operations Financial Invariant Audit", "[fuzzing]") {
    constexpr size_t NUM_OPERATIONS = 100000;
    constexpr uint64_t SEED = 0xDEADBEEFCAFE1234ULL;

    OrderBook book(NUM_OPERATIONS * 2, 5000);
    FuzzTrackingListener listener;
    MatchingEngine engine(book, &listener);

    std::mt19937_64 rng(SEED);
    std::uniform_int_distribution<int> op_dist(0, 99);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int64_t> price_dist(900000, 1100000);  // $90.00 to $110.00
    std::uniform_int_distribution<uint64_t> vol_dist(1, 100);

    std::vector<OrderId> active_order_ids;
    active_order_ids.reserve(NUM_OPERATIONS);

    OrderId next_id = 1;
    Timestamp ts = 1000;

    Volume total_submitted_vol = 0;

    for (size_t op = 0; op < NUM_OPERATIONS; ++op) {
        int op_type = op_dist(rng);
        Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
        Price price = Price::from_raw(price_dist(rng));
        Volume vol = vol_dist(rng);
        ts += 10;

        if (op_type < 20 && !active_order_ids.empty()) {
            // Cancel an active order
            size_t idx = static_cast<size_t>(rng() % active_order_ids.size());
            OrderId cancel_id = active_order_ids[idx];

            // Swap and pop
            active_order_ids[idx] = active_order_ids.back();
            active_order_ids.pop_back();

            engine.cancel_order(cancel_id, ts);
        } else if (op_type < 65) {
            // Standard Limit Order (GTC)
            OrderId id = next_id++;
            total_submitted_vol += vol;
            if (engine.submit_order(id, side, price, vol, ts, OrderType::Limit)) {
                if (book.find_order(id) != nullptr) {
                    active_order_ids.push_back(id);
                }
            }
        } else if (op_type < 80) {
            // Market Order
            OrderId id = next_id++;
            total_submitted_vol += vol;
            engine.submit_order(id, side, Price::zero(), vol, ts, OrderType::Market);
        } else if (op_type < 90) {
            // IOC Order
            OrderId id = next_id++;
            total_submitted_vol += vol;
            engine.submit_order(id, side, price, vol, ts, OrderType::IOC);
        } else {
            // FOK Order
            OrderId id = next_id++;
            total_submitted_vol += vol;
            engine.submit_order(id, side, price, vol, ts, OrderType::FOK);
        }

        // Periodic structural consistency audit
        if ((op + 1) % 5000 == 0) {
            InvariantReport report = InvariantChecker::check_book_consistency(book);
            REQUIRE(report.passed);
            REQUIRE(InvariantChecker::check_no_crossed_book(book));
        }
    }

    // Final deep invariant audit
    InvariantReport final_report = InvariantChecker::check_book_consistency(book);
    REQUIRE(final_report.passed);
    REQUIRE(InvariantChecker::check_no_crossed_book(book));

    // Invariant: Conservation of Shares & Volume
    Volume total_resting_vol = book.bids().total_volume() + book.asks().total_volume();
    // Every trade matches 1 buyer share with 1 seller share
    // Therefore traded volume accounted for in submitted volume is 2 * total_traded_volume
    Volume accounted_volume =
        (2 * listener.total_traded_volume) + total_resting_vol + listener.total_cancelled_volume;

    // Any discrepancy can only be rejected market/FOK orders
    REQUIRE(accounted_volume <= total_submitted_vol);
    REQUIRE(book.order_count() == book.order_index().size());
}

TEST_CASE("Fuzzing - Hot Path Zero Dynamic Memory Allocations", "[fuzzing]") {
    constexpr size_t NUM_OPERATIONS = 25000;
    constexpr uint64_t SEED = 0x123456789ABCDEF0ULL;

    OrderBook book;
    book.preallocate(NUM_OPERATIONS * 2, 2000);

    FuzzTrackingListener listener;
    MatchingEngine engine(book, &listener);

    std::mt19937_64 rng(SEED);
    std::uniform_int_distribution<int> op_dist(0, 99);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int64_t> price_dist(950000, 1050000);
    std::uniform_int_distribution<uint64_t> vol_dist(5, 50);

    std::vector<OrderId> active_order_ids;
    active_order_ids.reserve(NUM_OPERATIONS);

    // Warm up active orders
    for (OrderId id = 1; id <= 1000; ++id) {
        Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
        Price price = Price::from_raw(price_dist(rng));
        engine.submit_order(id, side, price, 10, id * 10);
        if (book.find_order(id) != nullptr) {
            active_order_ids.push_back(id);
        }
    }

    OrderId next_id = 2000;
    Timestamp ts = 50000;

    // Hot path starts: Zero dynamic allocations allowed
    {
        AllocationGuard guard;

        for (size_t op = 0; op < NUM_OPERATIONS; ++op) {
            int op_type = op_dist(rng);
            Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            Price price = Price::from_raw(price_dist(rng));
            Volume vol = vol_dist(rng);
            ts += 5;

            if (op_type < 20 && !active_order_ids.empty()) {
                size_t idx = static_cast<size_t>(rng() % active_order_ids.size());
                OrderId cancel_id = active_order_ids[idx];
                active_order_ids[idx] = active_order_ids.back();
                active_order_ids.pop_back();

                engine.cancel_order(cancel_id, ts);
            } else if (op_type < 60) {
                OrderId id = next_id++;
                if (engine.submit_order(id, side, price, vol, ts, OrderType::Limit)) {
                    if (book.find_order(id) != nullptr &&
                        active_order_ids.size() < NUM_OPERATIONS) {
                        active_order_ids.push_back(id);
                    }
                }
            } else if (op_type < 80) {
                OrderId id = next_id++;
                engine.submit_order(id, side, Price::zero(), vol, ts, OrderType::Market);
            } else if (op_type < 90) {
                OrderId id = next_id++;
                engine.submit_order(id, side, price, vol, ts, OrderType::IOC);
            } else {
                OrderId id = next_id++;
                engine.submit_order(id, side, price, vol, ts, OrderType::FOK);
            }
        }
    }

    REQUIRE(listener.total_trades_count > 0);
    REQUIRE(InvariantChecker::check_no_crossed_book(book));
}
