#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "orderbook/alloc_guard.hpp"
#include "orderbook/engine_service.hpp"
#include "orderbook/feed_handler.hpp"
#include "orderbook/market_data_publisher.hpp"
#include "orderbook/matching_engine.hpp"
#include "orderbook/order_book.hpp"

using namespace orderbook;

namespace {

class AmendTrackingListener : public IMatchingListener {
public:
    std::vector<OrderAccepted> accepted_events;
    std::vector<OrderRested> rested_events;
    std::vector<OrderRejected> rejected_events;
    std::vector<OrderCancelled> cancelled_events;
    std::vector<OrderFilled> filled_events;
    std::vector<OrderAmended> amended_events;
    std::vector<TradeEvent> trade_events;

    void on_order_accepted(const OrderAccepted& event) noexcept override {
        accepted_events.push_back(event);
    }
    void on_order_rested(const OrderRested& event) noexcept override {
        rested_events.push_back(event);
    }
    void on_order_rejected(const OrderRejected& event) noexcept override {
        rejected_events.push_back(event);
    }
    void on_order_cancelled(const OrderCancelled& event) noexcept override {
        cancelled_events.push_back(event);
    }
    void on_order_filled(const OrderFilled& event) noexcept override {
        filled_events.push_back(event);
    }
    void on_order_amended(const OrderAmended& event) noexcept override {
        amended_events.push_back(event);
    }
    void on_trade(const TradeEvent& event) noexcept override { trade_events.push_back(event); }

    void clear() noexcept {
        accepted_events.clear();
        rested_events.clear();
        rejected_events.clear();
        cancelled_events.clear();
        filled_events.clear();
        amended_events.clear();
        trade_events.clear();
    }
};

class MockFeedSink : public IBinaryFeedSink {
public:
    std::vector<AddOrderMsg> add_msgs;
    std::vector<OrderExecutedMsg> executed_msgs;
    std::vector<OrderDeleteMsg> deleted_msgs;
    std::vector<OrderReplaceMsg> replace_msgs;

    void on_add_order(const AddOrderMsg& msg) noexcept override { add_msgs.push_back(msg); }
    void on_order_executed(const OrderExecutedMsg& msg) noexcept override {
        executed_msgs.push_back(msg);
    }
    void on_order_deleted(const OrderDeleteMsg& msg) noexcept override {
        deleted_msgs.push_back(msg);
    }
    void on_order_replaced(const OrderReplaceMsg& msg) noexcept override {
        replace_msgs.push_back(msg);
    }
};

}  // namespace

TEST_CASE("Order Amend - Volume Reduction Retains FIFO Priority", "[amend]") {
    OrderBook book(1000, 100);
    AmendTrackingListener listener;
    MatchingEngine engine(book, &listener);

    Price p = Price::from_double(100.0);

    // Insert Order 1 and Order 2 at identical price
    REQUIRE(engine.submit_order(101, Side::Buy, p, 50, 1000));
    REQUIRE(engine.submit_order(102, Side::Buy, p, 50, 1001));

    REQUIRE(book.best_bid_volume() == 100);

    // Amend Order 1 volume from 50 down to 30 (reducing volume)
    REQUIRE(engine.amend_order(101, p, 30, 1002));
    REQUIRE(book.best_bid_volume() == 80);

    REQUIRE(listener.amended_events.size() == 1);
    CHECK(listener.amended_events[0].order_id == 101);
    CHECK(listener.amended_events[0].old_quantity == 50);
    CHECK(listener.amended_events[0].new_quantity == 30);
    CHECK(listener.amended_events[0].old_price == p);
    CHECK(listener.amended_events[0].new_price == p);

    // Incoming Sell order for 40 should execute 30 against Order 1 first, then 10 against Order 2
    REQUIRE(engine.submit_order(201, Side::Sell, p, 40, 1003));

    REQUIRE(listener.trade_events.size() == 2);
    CHECK(listener.trade_events[0].maker_order_id == 101);
    CHECK(listener.trade_events[0].quantity == 30);

    CHECK(listener.trade_events[1].maker_order_id == 102);
    CHECK(listener.trade_events[1].quantity == 10);

    // Order 1 was fully filled; Order 2 remains with 40
    CHECK(book.find_order(101) == nullptr);
    Order* rem = book.find_order(102);
    REQUIRE(rem != nullptr);
    CHECK(rem->remaining_volume == 40);
    CHECK(book.best_bid_volume() == 40);
}

TEST_CASE("Order Amend - Volume Increase Moves to Tail of Priority Queue", "[amend]") {
    OrderBook book(1000, 100);
    AmendTrackingListener listener;
    MatchingEngine engine(book, &listener);

    Price p = Price::from_double(100.0);

    // Insert Order 1 and Order 2 at identical price
    REQUIRE(engine.submit_order(101, Side::Buy, p, 50, 1000));
    REQUIRE(engine.submit_order(102, Side::Buy, p, 50, 1001));

    REQUIRE(book.best_bid_volume() == 100);

    // Amend Order 1 volume from 50 UP to 80 (loses priority, moves to tail)
    REQUIRE(engine.amend_order(101, p, 80, 1002));
    REQUIRE(book.best_bid_volume() == 130);

    REQUIRE(listener.amended_events.size() == 1);
    CHECK(listener.amended_events[0].order_id == 101);
    CHECK(listener.amended_events[0].old_quantity == 50);
    CHECK(listener.amended_events[0].new_quantity == 80);

    // Incoming Sell order for 50 must execute against Order 2 FIRST because Order 1 was moved to
    // tail
    REQUIRE(engine.submit_order(201, Side::Sell, p, 50, 1003));

    REQUIRE(listener.trade_events.size() == 1);
    CHECK(listener.trade_events[0].maker_order_id == 102);
    CHECK(listener.trade_events[0].quantity == 50);

    // Order 2 is filled; Order 1 now remains at head with 80
    CHECK(book.find_order(102) == nullptr);
    Order* rem = book.find_order(101);
    REQUIRE(rem != nullptr);
    CHECK(rem->remaining_volume == 80);
    CHECK(book.best_bid_volume() == 80);
}

TEST_CASE("Order Amend - Price Change Atomic Cancel-Replace", "[amend]") {
    OrderBook book(1000, 100);
    AmendTrackingListener listener;
    MatchingEngine engine(book, &listener);

    Price p100 = Price::from_double(100.0);
    Price p101 = Price::from_double(101.0);

    REQUIRE(engine.submit_order(101, Side::Buy, p100, 50, 1000));
    CHECK(book.best_bid() == p100);

    // Amend to higher price $101
    REQUIRE(engine.amend_order(101, p101, 60, 1001));

    CHECK(book.best_bid() == p101);
    CHECK(book.best_bid_volume() == 60);

    REQUIRE(listener.amended_events.size() == 1);
    CHECK(listener.amended_events[0].order_id == 101);
    CHECK(listener.amended_events[0].old_price == p100);
    CHECK(listener.amended_events[0].new_price == p101);
    CHECK(listener.amended_events[0].old_quantity == 50);
    CHECK(listener.amended_events[0].new_quantity == 60);
}

TEST_CASE("Order Amend - Price Change That Crosses Opposing Book", "[amend]") {
    OrderBook book(1000, 100);
    AmendTrackingListener listener;
    MatchingEngine engine(book, &listener);

    Price ask_price = Price::from_double(102.0);
    Price bid_old = Price::from_double(100.0);
    Price bid_new = Price::from_double(103.0);

    // Resting Ask at $102 for 100
    REQUIRE(engine.submit_order(501, Side::Sell, ask_price, 100, 1000));

    // Resting Bid at $100 for 40
    REQUIRE(engine.submit_order(502, Side::Buy, bid_old, 40, 1001));

    // Amend Bid to $103 (crossing the ask at $102)
    REQUIRE(engine.amend_order(502, bid_new, 40, 1002));

    // Must match immediately at maker price $102
    REQUIRE(listener.trade_events.size() == 1);
    CHECK(listener.trade_events[0].maker_order_id == 501);
    CHECK(listener.trade_events[0].taker_order_id == 502);
    CHECK(listener.trade_events[0].price == ask_price);
    CHECK(listener.trade_events[0].quantity == 40);

    // Remaining Ask has 60 volume
    CHECK(book.best_ask_volume() == 60);
    CHECK(!book.has_bids());
}

TEST_CASE("Order Amend - Invalid Arguments Rejections", "[amend]") {
    OrderBook book(1000, 100);
    AmendTrackingListener listener;
    MatchingEngine engine(book, &listener);

    Price p = Price::from_double(100.0);
    REQUIRE(engine.submit_order(101, Side::Buy, p, 50, 1000));

    // Non-existent order
    CHECK(!engine.amend_order(999, p, 50, 1001));
    REQUIRE(listener.rejected_events.size() == 1);
    CHECK(listener.rejected_events.back().reason == RejectReason::OrderNotFound);

    // Zero quantity
    CHECK(!engine.amend_order(101, p, 0, 1002));
    REQUIRE(listener.rejected_events.size() == 2);
    CHECK(listener.rejected_events.back().reason == RejectReason::InvalidQuantity);

    // Invalid negative or zero price
    CHECK(!engine.amend_order(101, Price::zero(), 50, 1003));
    REQUIRE(listener.rejected_events.size() == 3);
    CHECK(listener.rejected_events.back().reason == RejectReason::InvalidPrice);
}

class CountingAmendListener : public IMatchingListener {
public:
    size_t amend_count{0};
    size_t trade_count{0};
    size_t cancel_count{0};
    size_t accept_count{0};

    void on_order_amended(const OrderAmended&) noexcept override { ++amend_count; }
    void on_trade(const TradeEvent&) noexcept override { ++trade_count; }
    void on_order_cancelled(const OrderCancelled&) noexcept override { ++cancel_count; }
    void on_order_accepted(const OrderAccepted&) noexcept override { ++accept_count; }
};

TEST_CASE("Order Amend - Hot Path Zero Dynamic Memory Allocations", "[amend]") {
    OrderBook book(5000, 500);
    book.preallocate(5000, 500);
    CountingAmendListener listener;
    MatchingEngine engine(book, &listener);

    Price base_p = Price::from_double(100.0);

    // Warm-up order insertions
    for (uint64_t id = 1; id <= 100; ++id) {
        engine.submit_order(id, Side::Buy, base_p + Price::from_raw(static_cast<int64_t>(id % 10)),
                            50, 1000 + id);
    }

    {
        AllocationGuard guard;

        // Perform in-place amendments (volume reduction, volume increase, price change)
        for (uint64_t id = 1; id <= 100; ++id) {
            if (id % 3 == 0) {
                // Volume reduction
                engine.amend_order(id, base_p + Price::from_raw(static_cast<int64_t>(id % 10)), 25,
                                   2000 + id);
            } else if (id % 3 == 1) {
                // Volume increase
                engine.amend_order(id, base_p + Price::from_raw(static_cast<int64_t>(id % 10)), 75,
                                   2000 + id);
            } else {
                // Price change
                engine.amend_order(id, base_p + Price::from_raw(static_cast<int64_t>(id % 10 + 20)),
                                   50, 2000 + id);
            }
        }
    }

    CHECK(listener.amend_count == 100);
    SUCCEED("Amended 100 orders with zero heap allocations");
}

TEST_CASE("Order Amend - MarketDataPublisher and FeedHandler OrderReplaceMsg", "[amend]") {
    OrderBook book(1000, 100);
    MockFeedSink sink;
    MarketDataPublisher publisher(&sink);
    MatchingEngine engine(book, &publisher);

    Price p = Price::from_double(100.0);
    REQUIRE(engine.submit_order(101, Side::Buy, p, 50, 1000));
    REQUIRE(sink.add_msgs.size() == 1);

    // Amend order volume and price
    Price p_new = Price::from_double(100.50);
    REQUIRE(engine.amend_order(101, p_new, 35, 1001));

    // Verify MarketDataPublisher generated OrderReplaceMsg
    REQUIRE(sink.replace_msgs.size() == 1);
    const auto& replace_msg = sink.replace_msgs[0];
    CHECK(replace_msg.msg_type == MarketDataMsgType::OrderReplace);
    CHECK(replace_msg.order_id == 101);
    CHECK(replace_msg.new_price_raw == p_new.raw());
    CHECK(replace_msg.new_volume == 35);
    CHECK(replace_msg.timestamp == 1001);

    // Verify FeedHandler parses OrderReplaceMsg correctly
    FeedHandler feed_handler;
    REQUIRE(feed_handler.process_message(reinterpret_cast<const uint8_t*>(&sink.add_msgs[0]),
                                         sizeof(AddOrderMsg)));
    REQUIRE(feed_handler.process_message(reinterpret_cast<const uint8_t*>(&replace_msg),
                                         sizeof(OrderReplaceMsg)));
    CHECK(feed_handler.messages_processed() == 2);
    CHECK(feed_handler.is_in_sync());
    CHECK(!feed_handler.has_detected_gaps());
}

TEST_CASE("Order Amend - EngineService Asynchronous Ingestion", "[amend]") {
    OrderBook book(1000, 100);
    MatchingEngine engine(book);
    EngineService service(engine, 1024);

    service.start();

    Price p = Price::from_double(100.0);
    REQUIRE(service.submit_order(101, Side::Buy, p, 50, 1000));
    REQUIRE(service.amend_order(101, p, 20, 1001));

    service.stop();
    service.join();

    CHECK(service.processed_commands() == 2);
    Order* order = book.find_order(101);
    REQUIRE(order != nullptr);
    CHECK(order->remaining_volume == 20);
    CHECK(book.best_bid_volume() == 20);
}
