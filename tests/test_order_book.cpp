#include <catch2/catch_test_macros.hpp>

#include "orderbook/alloc_guard.hpp"
#include "orderbook/order_book.hpp"

using namespace orderbook;

TEST_CASE("OrderBook - Basic Limit Order Insertion and BBO", "[order_book]") {
    OrderBook book(100, 20);

    REQUIRE(book.is_empty());
    REQUIRE_FALSE(book.has_bids());
    REQUIRE_FALSE(book.has_asks());

    // Add bids
    Order* b1 = book.add_limit_order(1, Side::Buy, Price::from_double(100.0), 10, 1000);
    Order* b2 = book.add_limit_order(2, Side::Buy, Price::from_double(101.5), 20, 1001);
    REQUIRE(b1 != nullptr);
    REQUIRE(b2 != nullptr);
    REQUIRE(book.has_bids());
    REQUIRE(book.best_bid() == Price::from_double(101.5));
    REQUIRE(book.best_bid_volume() == 20);
    REQUIRE(book.order_count() == 2);

    // Add asks
    Order* a1 = book.add_limit_order(3, Side::Sell, Price::from_double(103.0), 15, 1002);
    Order* a2 = book.add_limit_order(4, Side::Sell, Price::from_double(102.5), 25, 1003);
    REQUIRE(a1 != nullptr);
    REQUIRE(a2 != nullptr);
    REQUIRE(book.has_asks());
    REQUIRE(book.best_ask() == Price::from_double(102.5));
    REQUIRE(book.best_ask_volume() == 25);
    REQUIRE(book.order_count() == 4);

    // Duplicate ID rejection
    REQUIRE(book.add_limit_order(1, Side::Buy, Price::from_double(100.0), 10, 1004) == nullptr);
    REQUIRE(book.order_count() == 4);
}

TEST_CASE("OrderBook - Order Cancellation and Depth Snapshots", "[order_book]") {
    OrderBook book(100, 20);

    book.add_limit_order(1, Side::Buy, Price::from_double(100.0), 10, 1000);
    book.add_limit_order(2, Side::Buy, Price::from_double(100.0), 20, 1001);
    book.add_limit_order(3, Side::Buy, Price::from_double(99.0), 30, 1002);
    book.add_limit_order(4, Side::Sell, Price::from_double(101.0), 15, 1003);

    REQUIRE(book.order_count() == 4);
    REQUIRE(book.best_bid_volume() == 30);

    // Cancel first order at 100.0
    REQUIRE(book.cancel_order(1));
    REQUIRE(book.order_count() == 3);
    REQUIRE(book.best_bid() == Price::from_double(100.0));
    REQUIRE(book.best_bid_volume() == 20);

    // Cancel second order at 100.0 - cleans up level
    REQUIRE(book.cancel_order(2));
    REQUIRE(book.order_count() == 2);
    REQUIRE(book.best_bid() == Price::from_double(99.0));
    REQUIRE(book.best_bid_volume() == 30);

    // Cancel non-existent
    REQUIRE_FALSE(book.cancel_order(9999));

    // Depth snapshot
    auto bids_depth = book.get_bids_depth(5);
    REQUIRE(bids_depth.size() == 1);
    REQUIRE(bids_depth[0].price == Price::from_double(99.0));
    REQUIRE(bids_depth[0].volume == 30);

    auto asks_depth = book.get_asks_depth(5);
    REQUIRE(asks_depth.size() == 1);
    REQUIRE(asks_depth[0].price == Price::from_double(101.0));
    REQUIRE(asks_depth[0].volume == 15);
}

TEST_CASE("OrderBook - Zero Heap Allocation Hot Path Proof", "[order_book]") {
    OrderBook book(5000, 500);

    // Enter guarded hot path: zero malloc/new calls allowed
    {
        AllocationGuard guard;

        // Insert 200 bids across 20 distinct price levels
        for (uint64_t i = 1; i <= 200; ++i) {
            Price price = Price::from_double(100.0 - static_cast<double>(i % 20));
            Order* o = book.add_limit_order(i, Side::Buy, price, 10, i);
            (void)o;
        }

        // Insert 200 asks across 20 distinct price levels
        for (uint64_t i = 201; i <= 400; ++i) {
            Price price = Price::from_double(101.0 + static_cast<double>(i % 20));
            Order* o = book.add_limit_order(i, Side::Sell, price, 15, i);
            (void)o;
        }

        // Query BBO
        Price bid = book.best_bid();
        Price ask = book.best_ask();
        (void)bid;
        (void)ask;

        // Cancel 100 bids and 100 asks
        for (uint64_t i = 1; i <= 100; ++i) {
            book.cancel_order(i);
        }
        for (uint64_t i = 201; i <= 300; ++i) {
            book.cancel_order(i);
        }
    }

    // Verify order count after guarded execution
    REQUIRE(book.order_count() == 200);
}
