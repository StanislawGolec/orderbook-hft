#include <catch2/catch_test_macros.hpp>

#include "orderbook/price_level.hpp"

using namespace orderbook;

TEST_CASE("PriceLevel - Initialization and Properties", "[price_level]") {
    PriceLevel level(Price::from_double(100.50));
    REQUIRE(level.price() == Price::from_double(100.50));
    REQUIRE(level.empty());
    REQUIRE(level.order_count() == 0);
    REQUIRE(level.total_volume() == 0);

    level.set_price(Price::from_double(101.00));
    REQUIRE(level.price() == Price::from_double(101.00));
}

TEST_CASE("PriceLevel - Order Management and Volume Tracking", "[price_level]") {
    PriceLevel level(Price::from_double(50.00));

    Order o1{.id = 1, .price = Price::from_double(50.00), .remaining_volume = 100};
    Order o2{.id = 2, .price = Price::from_double(50.00), .remaining_volume = 200};

    level.add_order(&o1);
    REQUIRE_FALSE(level.empty());
    REQUIRE(level.order_count() == 1);
    REQUIRE(level.total_volume() == 100);

    level.add_order(&o2);
    REQUIRE(level.order_count() == 2);
    REQUIRE(level.total_volume() == 300);

    // Partial fill volume reduction
    o1.fill(50);
    level.reduce_volume(50);
    REQUIRE(level.total_volume() == 250);

    // Remove first order
    level.remove_order(&o1);
    REQUIRE(level.order_count() == 1);
    REQUIRE(level.total_volume() == 200);

    // Remove second order
    level.remove_order(&o2);
    REQUIRE(level.empty());
    REQUIRE(level.order_count() == 0);
    REQUIRE(level.total_volume() == 0);
}
