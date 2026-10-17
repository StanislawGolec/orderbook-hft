#include <catch2/catch_test_macros.hpp>

#include "orderbook/order.hpp"

using namespace orderbook;

TEST_CASE("Order - Layout and Cache Alignment", "[order]") {
    STATIC_REQUIRE(sizeof(Order) == 64);
    STATIC_REQUIRE(alignof(Order) == 64);
    STATIC_REQUIRE(std::is_standard_layout_v<Order>);

    Order order;
    REQUIRE(order.id == INVALID_ORDER_ID);
    REQUIRE(order.price == Price::zero());
    REQUIRE(order.initial_volume == 0);
    REQUIRE(order.remaining_volume == 0);
    REQUIRE(order.timestamp == 0);
    REQUIRE(order.next == nullptr);
    REQUIRE(order.prev == nullptr);
    REQUIRE(order.side == Side::Buy);
    REQUIRE(order.type == OrderType::Limit);
    REQUIRE(order.tif == TimeInForce::GTC);
}

TEST_CASE("Order - Volume Fill Logic", "[order]") {
    Order order{.id = 101,
                .price = Price::from_double(150.25),
                .initial_volume = 100,
                .remaining_volume = 100,
                .timestamp = 5000,
                .side = Side::Buy,
                .type = OrderType::Limit,
                .tif = TimeInForce::GTC};

    REQUIRE_FALSE(order.is_filled());
    REQUIRE(order.filled_volume() == 0);

    // Partial fill
    order.fill(30);
    REQUIRE_FALSE(order.is_filled());
    REQUIRE(order.remaining_volume == 70);
    REQUIRE(order.filled_volume() == 30);

    // Fill the remainder
    order.fill(70);
    REQUIRE(order.is_filled());
    REQUIRE(order.remaining_volume == 0);
    REQUIRE(order.filled_volume() == 100);

    // Oversized fill clamps to zero
    order.fill(50);
    REQUIRE(order.is_filled());
    REQUIRE(order.remaining_volume == 0);
}
