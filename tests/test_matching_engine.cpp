#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "orderbook/alloc_guard.hpp"
#include "orderbook/matching_engine.hpp"

using namespace orderbook;

namespace {

class RecordingListener : public IMatchingListener {
public:
    std::vector<OrderAccepted> accepted;
    std::vector<OrderRejected> rejected;
    std::vector<OrderCancelled> cancelled;
    std::vector<OrderFilled> filled;
    std::vector<TradeEvent> trades;

    void on_order_accepted(const OrderAccepted& event) noexcept override {
        accepted.push_back(event);
    }

    void on_order_rejected(const OrderRejected& event) noexcept override {
        rejected.push_back(event);
    }

    void on_order_cancelled(const OrderCancelled& event) noexcept override {
        cancelled.push_back(event);
    }

    void on_order_filled(const OrderFilled& event) noexcept override { filled.push_back(event); }

    void on_trade(const TradeEvent& event) noexcept override { trades.push_back(event); }

    void clear() {
        accepted.clear();
        rejected.clear();
        cancelled.clear();
        filled.clear();
        trades.clear();
    }
};

class CounterListener : public IMatchingListener {
public:
    size_t accept_count{0};
    size_t reject_count{0};
    size_t cancel_count{0};
    size_t fill_count{0};
    size_t trade_count{0};

    void on_order_accepted(const OrderAccepted&) noexcept override { ++accept_count; }
    void on_order_rejected(const OrderRejected&) noexcept override { ++reject_count; }
    void on_order_cancelled(const OrderCancelled&) noexcept override { ++cancel_count; }
    void on_order_filled(const OrderFilled&) noexcept override { ++fill_count; }
    void on_trade(const TradeEvent&) noexcept override { ++trade_count; }

    void reset() noexcept {
        accept_count = 0;
        reject_count = 0;
        cancel_count = 0;
        fill_count = 0;
        trade_count = 0;
    }
};

}  // namespace

TEST_CASE("MatchingEngine - Basic Resting Limit Orders", "[matching_engine]") {
    OrderBook book(1024, 128);
    RecordingListener listener;
    MatchingEngine engine(book, &listener);

    // Add Bid @ 100.0, qty 10
    bool res1 = engine.submit_order(1, Side::Buy, Price::from_double(100.0), 10, 1000);
    REQUIRE(res1);
    REQUIRE(listener.accepted.size() == 1);
    REQUIRE(listener.trades.empty());
    REQUIRE(book.best_bid() == Price::from_double(100.0));
    REQUIRE(book.best_bid_volume() == 10);

    // Add Ask @ 102.0, qty 20
    bool res2 = engine.submit_order(2, Side::Sell, Price::from_double(102.0), 20, 2000);
    REQUIRE(res2);
    REQUIRE(listener.accepted.size() == 2);
    REQUIRE(listener.trades.empty());
    REQUIRE(book.best_ask() == Price::from_double(102.0));
    REQUIRE(book.best_ask_volume() == 20);

    REQUIRE(book.order_count() == 2);
}

TEST_CASE("MatchingEngine - Limit Order Crossing and Maker Price Priority", "[matching_engine]") {
    OrderBook book(1024, 128);
    RecordingListener listener;
    MatchingEngine engine(book, &listener);

    // Resting Ask: Order 1, Sell 10 @ 100.0
    REQUIRE(engine.submit_order(1, Side::Sell, Price::from_double(100.0), 10, 1000));

    // Incoming aggressive Bid: Order 2, Buy 10 @ 105.0 (crosses at maker price 100.0)
    REQUIRE(engine.submit_order(2, Side::Buy, Price::from_double(105.0), 10, 2000));

    REQUIRE(listener.accepted.size() == 2);
    REQUIRE(listener.trades.size() == 1);

    const auto& trade = listener.trades[0];
    REQUIRE(trade.match_id == 1);
    REQUIRE(trade.maker_order_id == 1);
    REQUIRE(trade.taker_order_id == 2);
    REQUIRE(trade.price == Price::from_double(100.0));  // Maker price priority!
    REQUIRE(trade.quantity == 10);
    REQUIRE(trade.taker_side == Side::Buy);

    // OrderFilled events (one for maker, one for taker)
    REQUIRE(listener.filled.size() == 2);
    REQUIRE(listener.filled[0].order_id == 1);
    REQUIRE(listener.filled[0].remaining_quantity == 0);
    REQUIRE(listener.filled[1].order_id == 2);
    REQUIRE(listener.filled[1].remaining_quantity == 0);

    // Book should now be empty
    REQUIRE(book.is_empty());
    REQUIRE(book.order_count() == 0);
}

TEST_CASE("MatchingEngine - Partial Fills and Resting Remainder", "[matching_engine]") {
    OrderBook book(1024, 128);
    RecordingListener listener;
    MatchingEngine engine(book, &listener);

    // Resting Ask: Order 1, Sell 30 @ 100.0
    REQUIRE(engine.submit_order(1, Side::Sell, Price::from_double(100.0), 30, 1000));

    // Incoming Bid: Order 2, Buy 50 @ 100.0
    REQUIRE(engine.submit_order(2, Side::Buy, Price::from_double(100.0), 50, 2000));

    // 30 filled against Order 1, remaining 20 of Order 2 rests as Bid @ 100.0
    REQUIRE(listener.trades.size() == 1);
    REQUIRE(listener.trades[0].quantity == 30);
    REQUIRE(listener.trades[0].price == Price::from_double(100.0));

    REQUIRE(!book.has_asks());
    REQUIRE(book.has_bids());
    REQUIRE(book.best_bid() == Price::from_double(100.0));
    REQUIRE(book.best_bid_volume() == 20);
    REQUIRE(book.order_count() == 1);

    Order* resting = book.find_order(2);
    REQUIRE(resting != nullptr);
    REQUIRE(resting->initial_volume == 50);
    REQUIRE(resting->remaining_volume == 20);
}

TEST_CASE("MatchingEngine - FIFO Time Priority at Same Price Level", "[matching_engine]") {
    OrderBook book(1024, 128);
    RecordingListener listener;
    MatchingEngine engine(book, &listener);

    // Order 1 arrives first: Sell 10 @ 100.0
    REQUIRE(engine.submit_order(1, Side::Sell, Price::from_double(100.0), 10, 1000));
    // Order 2 arrives second: Sell 20 @ 100.0
    REQUIRE(engine.submit_order(2, Side::Sell, Price::from_double(100.0), 20, 1001));

    // Incoming Bid: Order 3, Buy 15 @ 100.0
    REQUIRE(engine.submit_order(3, Side::Buy, Price::from_double(100.0), 15, 2000));

    // Order 1 filled completely (10), Order 2 filled partially (5)
    REQUIRE(listener.trades.size() == 2);
    REQUIRE(listener.trades[0].maker_order_id == 1);
    REQUIRE(listener.trades[0].quantity == 10);
    REQUIRE(listener.trades[1].maker_order_id == 2);
    REQUIRE(listener.trades[1].quantity == 5);

    // Order 1 gone, Order 2 has 15 remaining
    REQUIRE(book.find_order(1) == nullptr);
    Order* rem = book.find_order(2);
    REQUIRE(rem != nullptr);
    REQUIRE(rem->remaining_volume == 15);
    REQUIRE(book.best_ask_volume() == 15);
}

TEST_CASE("MatchingEngine - Multi-Level Market Order Sweep", "[matching_engine]") {
    OrderBook book(1024, 128);
    RecordingListener listener;
    MatchingEngine engine(book, &listener);

    // Populate book with 3 Ask levels
    REQUIRE(engine.submit_order(1, Side::Sell, Price::from_double(100.0), 10, 1000));
    REQUIRE(engine.submit_order(2, Side::Sell, Price::from_double(101.0), 15, 1001));
    REQUIRE(engine.submit_order(3, Side::Sell, Price::from_double(102.0), 25, 1002));

    // Aggressive Market Buy: 35 shares
    bool res = engine.submit_order(100, Side::Buy, Price::zero(), 35, 2000, OrderType::Market);
    REQUIRE(res);

    REQUIRE(listener.trades.size() == 3);
    // Sweeps 10 @ 100.0
    REQUIRE(listener.trades[0].price == Price::from_double(100.0));
    REQUIRE(listener.trades[0].quantity == 10);
    // Sweeps 15 @ 101.0
    REQUIRE(listener.trades[1].price == Price::from_double(101.0));
    REQUIRE(listener.trades[1].quantity == 15);
    // Sweeps 10 @ 102.0
    REQUIRE(listener.trades[2].price == Price::from_double(102.0));
    REQUIRE(listener.trades[2].quantity == 10);

    // Level 102.0 has 15 remaining
    REQUIRE(book.best_ask() == Price::from_double(102.0));
    REQUIRE(book.best_ask_volume() == 15);
}

TEST_CASE("MatchingEngine - Market Order Empty Book Rejection", "[matching_engine]") {
    OrderBook book(1024, 128);
    RecordingListener listener;
    MatchingEngine engine(book, &listener);

    // Market Buy on empty book
    bool res = engine.submit_order(1, Side::Buy, Price::zero(), 100, 1000, OrderType::Market);
    REQUIRE(!res);
    REQUIRE(listener.rejected.size() == 1);
    REQUIRE(listener.rejected[0].reason == RejectReason::InsufficientLiquidity);
}

TEST_CASE("MatchingEngine - Market Order Partial Fill and Remainder Cancellation",
          "[matching_engine]") {
    OrderBook book(1024, 128);
    RecordingListener listener;
    MatchingEngine engine(book, &listener);

    // Ask: 20 @ 100.0
    REQUIRE(engine.submit_order(1, Side::Sell, Price::from_double(100.0), 20, 1000));

    // Market Buy 50 shares
    REQUIRE(engine.submit_order(2, Side::Buy, Price::zero(), 50, 2000, OrderType::Market));

    REQUIRE(listener.trades.size() == 1);
    REQUIRE(listener.trades[0].quantity == 20);

    // Remainder 30 cancelled
    REQUIRE(listener.cancelled.size() == 1);
    REQUIRE(listener.cancelled[0].order_id == 2);
    REQUIRE(listener.cancelled[0].remaining_quantity == 30);
    REQUIRE(book.is_empty());
}

TEST_CASE("MatchingEngine - Immediate-Or-Cancel (IOC) Execution", "[matching_engine]") {
    OrderBook book(1024, 128);
    RecordingListener listener;
    MatchingEngine engine(book, &listener);

    // Resting Ask: 15 @ 100.0
    REQUIRE(engine.submit_order(1, Side::Sell, Price::from_double(100.0), 15, 1000));

    // IOC Buy: 40 @ 100.0
    REQUIRE(engine.submit_order(2, Side::Buy, Price::from_double(100.0), 40, 2000, OrderType::IOC));

    // 15 matched, 25 cancelled
    REQUIRE(listener.trades.size() == 1);
    REQUIRE(listener.trades[0].quantity == 15);
    REQUIRE(listener.cancelled.size() == 1);
    REQUIRE(listener.cancelled[0].order_id == 2);
    REQUIRE(listener.cancelled[0].remaining_quantity == 25);

    REQUIRE(book.is_empty());
}

TEST_CASE("MatchingEngine - Fill-Or-Kill (FOK) Atomic Execution and Rejection",
          "[matching_engine]") {
    OrderBook book(1024, 128);
    RecordingListener listener;
    MatchingEngine engine(book, &listener);

    // Resting Ask: 25 @ 100.0
    REQUIRE(engine.submit_order(1, Side::Sell, Price::from_double(100.0), 25, 1000));

    // Case 1: FOK Buy 30 @ 100.0 (insufficient depth: 25 < 30) -> Atomic Rejection
    bool res1 =
        engine.submit_order(2, Side::Buy, Price::from_double(100.0), 30, 2000, OrderType::FOK);
    REQUIRE(!res1);
    REQUIRE(listener.trades.empty());
    REQUIRE(listener.rejected.size() == 1);
    REQUIRE(listener.rejected[0].order_id == 2);
    REQUIRE(listener.rejected[0].reason == RejectReason::InsufficientLiquidity);

    // Book was completely untouched!
    REQUIRE(book.best_ask_volume() == 25);

    // Case 2: FOK Buy 25 @ 100.0 (exact depth) -> Complete Fill
    bool res2 =
        engine.submit_order(3, Side::Buy, Price::from_double(100.0), 25, 3000, OrderType::FOK);
    REQUIRE(res2);
    REQUIRE(listener.trades.size() == 1);
    REQUIRE(listener.trades[0].quantity == 25);
    REQUIRE(book.is_empty());
}

TEST_CASE("MatchingEngine - Order Cancellation by ID", "[matching_engine]") {
    OrderBook book(1024, 128);
    RecordingListener listener;
    MatchingEngine engine(book, &listener);

    REQUIRE(engine.submit_order(1, Side::Buy, Price::from_double(100.0), 10, 1000));
    REQUIRE(book.order_count() == 1);

    // Cancel existing
    bool cancelled = engine.cancel_order(1, 2000);
    REQUIRE(cancelled);
    REQUIRE(listener.cancelled.size() == 1);
    REQUIRE(listener.cancelled[0].order_id == 1);
    REQUIRE(listener.cancelled[0].remaining_quantity == 10);
    REQUIRE(book.is_empty());

    // Cancel non-existing
    bool failed_cancel = engine.cancel_order(999, 3000);
    REQUIRE(!failed_cancel);
    REQUIRE(listener.rejected.size() == 1);
    REQUIRE(listener.rejected[0].order_id == 999);
    REQUIRE(listener.rejected[0].reason == RejectReason::OrderNotFound);
}

TEST_CASE("MatchingEngine - Validation and Duplicate Order ID", "[matching_engine]") {
    OrderBook book(1024, 128);
    RecordingListener listener;
    MatchingEngine engine(book, &listener);

    // Zero volume
    REQUIRE(!engine.submit_order(1, Side::Buy, Price::from_double(100.0), 0, 1000));
    REQUIRE(listener.rejected.size() == 1);

    // Negative / zero price on limit order
    REQUIRE(!engine.submit_order(2, Side::Buy, Price::zero(), 10, 1000));
    REQUIRE(listener.rejected.size() == 2);

    // Duplicate order ID
    REQUIRE(engine.submit_order(3, Side::Buy, Price::from_double(100.0), 10, 1000));
    REQUIRE(!engine.submit_order(3, Side::Buy, Price::from_double(100.0), 10, 2000));
    REQUIRE(listener.rejected.size() == 3);
    REQUIRE(listener.rejected[2].reason == RejectReason::DuplicateOrderId);
}

TEST_CASE("MatchingEngine - Zero Heap Allocation on Hot Path", "[matching_engine]") {
    OrderBook book;
    book.preallocate(10000, 1000);

    CounterListener listener;
    MatchingEngine engine(book, &listener);

    // Pre-populate resting liquidity
    for (uint64_t id = 1; id <= 100; ++id) {
        Price p = Price::from_double(100.0 + static_cast<double>(id) * 0.1);
        engine.submit_order(id, Side::Sell, p, 100, id * 10);
    }

    // Hot path starts: Zero allocations allowed
    {
        AllocationGuard guard;

        // 1. Limit order resting insertion
        engine.submit_order(201, Side::Buy, Price::from_double(90.0), 50, 5000);

        // 2. Cancellation of resting order
        engine.cancel_order(201, 5010);

        // 3. Multi-level crossing sweep (sweeps 3 ask levels)
        engine.submit_order(202, Side::Buy, Price::from_double(100.35), 250, 6000);

        // 4. Market order sweep
        engine.submit_order(203, Side::Buy, Price::zero(), 100, 7000, OrderType::Market);

        // 5. IOC partial execution
        engine.submit_order(204, Side::Buy, Price::from_double(100.6), 500, 8000, OrderType::IOC);

        // 6. FOK full execution
        engine.submit_order(205, Side::Buy, Price::from_double(100.7), 100, 9000, OrderType::FOK);

        // 7. FOK rejected (insufficient depth)
        engine.submit_order(206, Side::Buy, Price::from_double(100.7), 999999, 9010,
                            OrderType::FOK);
    }

    REQUIRE(listener.trade_count > 0);
    REQUIRE(listener.accept_count > 0);
    REQUIRE(listener.cancel_count > 0);
    REQUIRE(listener.reject_count > 0);
}
