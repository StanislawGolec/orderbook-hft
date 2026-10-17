#include <catch2/catch_test_macros.hpp>

#include "orderbook/memory_pool.hpp"
#include "orderbook/order_book_side.hpp"

using namespace orderbook;

TEST_CASE("OrderBookSide - Bids Sorted Descending", "[order_book_side]") {
    ObjectPool<PriceLevel> level_pool(16);
    OrderBookSide bids(Side::Buy, level_pool);

    REQUIRE(bids.empty());
    REQUIRE(bids.side() == Side::Buy);
    REQUIRE(bids.best_level() == nullptr);

    Order o1{.id = 1, .price = Price::from_double(100.0), .remaining_volume = 10};
    Order o2{.id = 2, .price = Price::from_double(102.0), .remaining_volume = 20};
    Order o3{.id = 3, .price = Price::from_double(99.0), .remaining_volume = 15};

    bids.add_order(&o1);
    REQUIRE(bids.best_price() == Price::from_double(100.0));
    REQUIRE(bids.best_volume() == 10);

    bids.add_order(&o2);
    // Higher bid becomes best bid
    REQUIRE(bids.best_price() == Price::from_double(102.0));
    REQUIRE(bids.best_volume() == 20);

    bids.add_order(&o3);
    REQUIRE(bids.best_price() == Price::from_double(102.0));
    REQUIRE(bids.total_volume() == 45);
    REQUIRE(bids.level_count() == 3);

    // Depth check: 102.0, 100.0, 99.0
    auto depth = bids.get_depth(5);
    REQUIRE(depth.size() == 3);
    REQUIRE(depth[0].price == Price::from_double(102.0));
    REQUIRE(depth[1].price == Price::from_double(100.0));
    REQUIRE(depth[2].price == Price::from_double(99.0));

    // Remove best bid
    REQUIRE(bids.remove_order(&o2));
    REQUIRE(bids.best_price() == Price::from_double(100.0));
    REQUIRE(bids.level_count() == 2);
}

TEST_CASE("OrderBookSide - Asks Sorted Ascending", "[order_book_side]") {
    ObjectPool<PriceLevel> level_pool(16);
    OrderBookSide asks(Side::Sell, level_pool);

    REQUIRE(asks.side() == Side::Sell);

    Order o1{.id = 1, .price = Price::from_double(105.0), .remaining_volume = 10};
    Order o2{.id = 2, .price = Price::from_double(103.0), .remaining_volume = 25};
    Order o3{.id = 3, .price = Price::from_double(107.0), .remaining_volume = 30};

    asks.add_order(&o1);
    REQUIRE(asks.best_price() == Price::from_double(105.0));

    asks.add_order(&o2);
    // Lower ask becomes best ask
    REQUIRE(asks.best_price() == Price::from_double(103.0));
    REQUIRE(asks.best_volume() == 25);

    asks.add_order(&o3);
    REQUIRE(asks.best_price() == Price::from_double(103.0));
    REQUIRE(asks.total_volume() == 65);

    // Depth check: 103.0, 105.0, 107.0
    auto depth = asks.get_depth(5);
    REQUIRE(depth.size() == 3);
    REQUIRE(depth[0].price == Price::from_double(103.0));
    REQUIRE(depth[1].price == Price::from_double(105.0));
    REQUIRE(depth[2].price == Price::from_double(107.0));
}

TEST_CASE("OrderBookSide - Multiple Orders at Same Price FIFO Queue", "[order_book_side]") {
    ObjectPool<PriceLevel> level_pool(16);
    OrderBookSide bids(Side::Buy, level_pool);

    Order o1{.id = 1, .price = Price::from_double(100.0), .remaining_volume = 10};
    Order o2{.id = 2, .price = Price::from_double(100.0), .remaining_volume = 20};

    bids.add_order(&o1);
    bids.add_order(&o2);

    REQUIRE(bids.level_count() == 1);
    REQUIRE(bids.best_volume() == 30);

    PriceLevel* level = bids.best_level();
    REQUIRE(level->order_count() == 2);
    REQUIRE(level->orders().front() == &o1);
    REQUIRE(level->orders().back() == &o2);

    // Remove first order
    bids.remove_order(&o1);
    REQUIRE(bids.level_count() == 1);
    REQUIRE(bids.best_volume() == 20);
    REQUIRE(level->orders().front() == &o2);

    // Remove second order cleans up level
    bids.remove_order(&o2);
    REQUIRE(bids.empty());
    REQUIRE(bids.best_level() == nullptr);
}
