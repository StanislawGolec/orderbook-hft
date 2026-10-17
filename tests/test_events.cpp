#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include "orderbook/events.hpp"

using namespace orderbook;

TEST_CASE("Events - TradeEvent Construction and Layout", "[events]") {
    TradeEvent trade{.match_id = 1,
                     .maker_order_id = 100,
                     .taker_order_id = 200,
                     .price = Price::from_double(50.25),
                     .quantity = 10,
                     .taker_side = Side::Buy,
                     .timestamp = 1600000000};

    REQUIRE(trade.match_id == 1);
    REQUIRE(trade.maker_order_id == 100);
    REQUIRE(trade.taker_order_id == 200);
    REQUIRE(trade.price == Price::from_double(50.25));
    REQUIRE(trade.quantity == 10);
    REQUIRE(trade.taker_side == Side::Buy);
    REQUIRE(trade.timestamp == 1600000000);

    // Verify zero-overhead raw memory copying (POD)
    TradeEvent copy{};
    std::memcpy(&copy, &trade, sizeof(TradeEvent));
    REQUIRE(copy.match_id == trade.match_id);
    REQUIRE(copy.price == trade.price);
    REQUIRE(copy.quantity == trade.quantity);
}

TEST_CASE("Events - OrderAccepted Lifecycle", "[events]") {
    OrderAccepted accepted{.order_id = 1001,
                           .price = Price::from_double(10.0),
                           .quantity = 100,
                           .side = Side::Sell,
                           .type = OrderType::Limit,
                           .timestamp = 123456789};

    REQUIRE(accepted.order_id == 1001);
    REQUIRE(accepted.price == Price::from_double(10.0));
    REQUIRE(accepted.quantity == 100);
    REQUIRE(accepted.side == Side::Sell);
    REQUIRE(accepted.type == OrderType::Limit);
}

TEST_CASE("Events - OrderRejected Lifecycle", "[events]") {
    OrderRejected rejected{
        .order_id = 1002, .reason = RejectReason::InvalidPrice, .timestamp = 123456790};

    REQUIRE(rejected.order_id == 1002);
    REQUIRE(rejected.reason == RejectReason::InvalidPrice);
}

TEST_CASE("Events - OrderCancelled Lifecycle", "[events]") {
    OrderCancelled cancelled{.order_id = 1003,
                             .price = Price::from_double(99.0),
                             .remaining_quantity = 40,
                             .side = Side::Buy,
                             .timestamp = 123456791};

    REQUIRE(cancelled.order_id == 1003);
    REQUIRE(cancelled.price == Price::from_double(99.0));
    REQUIRE(cancelled.remaining_quantity == 40);
    REQUIRE(cancelled.side == Side::Buy);
}

TEST_CASE("Events - OrderFilled Lifecycle", "[events]") {
    OrderFilled filled{.order_id = 1004,
                       .price = Price::from_double(105.5),
                       .filled_quantity = 50,
                       .remaining_quantity = 0,
                       .timestamp = 123456792};

    REQUIRE(filled.order_id == 1004);
    REQUIRE(filled.price == Price::from_double(105.5));
    REQUIRE(filled.filled_quantity == 50);
    REQUIRE(filled.remaining_quantity == 0);
}
