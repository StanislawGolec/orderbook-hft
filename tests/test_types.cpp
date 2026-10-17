#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>

#include "orderbook/events.hpp"
#include "orderbook/types.hpp"

using namespace orderbook;

TEST_CASE("Domain Types - Identifier Constants", "[types]") {
    STATIC_REQUIRE(INVALID_ORDER_ID == 0);
    STATIC_REQUIRE(ZERO_VOLUME == 0);
    STATIC_REQUIRE(ZERO_TIMESTAMP == 0);

    OrderId id = 12345;
    Volume vol = 500;
    Timestamp ts = 1000000;

    REQUIRE(id == 12345);
    REQUIRE(vol == 500);
    REQUIRE(ts == 1000000);
}

TEST_CASE("Domain Types - Side Enum and Conversions", "[types]") {
    STATIC_REQUIRE(opposite_side(Side::Buy) == Side::Sell);
    STATIC_REQUIRE(opposite_side(Side::Sell) == Side::Buy);

    STATIC_REQUIRE(to_string(Side::Buy) == "BUY");
    STATIC_REQUIRE(to_string(Side::Sell) == "SELL");

    std::ostringstream oss;
    oss << Side::Buy << " " << Side::Sell;
    REQUIRE(oss.str() == "BUY SELL");
}

TEST_CASE("Domain Types - OrderType Enum and Conversions", "[types]") {
    STATIC_REQUIRE(to_string(OrderType::Limit) == "LIMIT");
    STATIC_REQUIRE(to_string(OrderType::Market) == "MARKET");
    STATIC_REQUIRE(to_string(OrderType::IOC) == "IOC");
    STATIC_REQUIRE(to_string(OrderType::FOK) == "FOK");

    std::ostringstream oss;
    oss << OrderType::Limit << " " << OrderType::IOC;
    REQUIRE(oss.str() == "LIMIT IOC");
}

TEST_CASE("Domain Types - TimeInForce Enum and Conversions", "[types]") {
    STATIC_REQUIRE(to_string(TimeInForce::GTC) == "GTC");
    STATIC_REQUIRE(to_string(TimeInForce::IOC) == "IOC");
    STATIC_REQUIRE(to_string(TimeInForce::FOK) == "FOK");

    std::ostringstream oss;
    oss << TimeInForce::GTC << " " << TimeInForce::FOK;
    REQUIRE(oss.str() == "GTC FOK");
}

TEST_CASE("Domain Types - OrderStatus Enum and Conversions", "[types]") {
    STATIC_REQUIRE(to_string(OrderStatus::New) == "NEW");
    STATIC_REQUIRE(to_string(OrderStatus::Accepted) == "ACCEPTED");
    STATIC_REQUIRE(to_string(OrderStatus::PartiallyFilled) == "PARTIALLY_FILLED");
    STATIC_REQUIRE(to_string(OrderStatus::Filled) == "FILLED");
    STATIC_REQUIRE(to_string(OrderStatus::Cancelled) == "CANCELLED");
    STATIC_REQUIRE(to_string(OrderStatus::Rejected) == "REJECTED");

    std::ostringstream oss;
    oss << OrderStatus::Accepted;
    REQUIRE(oss.str() == "ACCEPTED");
}

TEST_CASE("Domain Types - RejectReason Enum and Conversions", "[types]") {
    STATIC_REQUIRE(to_string(RejectReason::None) == "NONE");
    STATIC_REQUIRE(to_string(RejectReason::InvalidPrice) == "INVALID_PRICE");
    STATIC_REQUIRE(to_string(RejectReason::InvalidQuantity) == "INVALID_QUANTITY");
    STATIC_REQUIRE(to_string(RejectReason::OrderNotFound) == "ORDER_NOT_FOUND");
    STATIC_REQUIRE(to_string(RejectReason::InsufficientLiquidity) == "INSUFFICIENT_LIQUIDITY");
    STATIC_REQUIRE(to_string(RejectReason::DuplicateOrderId) == "DUPLICATE_ORDER_ID");
    STATIC_REQUIRE(to_string(RejectReason::BookFull) == "BOOK_FULL");

    std::ostringstream oss;
    oss << RejectReason::InsufficientLiquidity;
    REQUIRE(oss.str() == "INSUFFICIENT_LIQUIDITY");
}

TEST_CASE("Domain Types - C++20 Concepts Evaluation", "[types]") {
    STATIC_REQUIRE(NumericTick<int>);
    STATIC_REQUIRE(NumericTick<int64_t>);
    STATIC_REQUIRE(NumericTick<uint32_t>);
    STATIC_REQUIRE(NumericTick<double>);
    STATIC_REQUIRE(NumericTick<float>);
    STATIC_REQUIRE_FALSE(NumericTick<std::string>);

    STATIC_REQUIRE(SerializableEvent<TradeEvent>);
    STATIC_REQUIRE(SerializableEvent<OrderAccepted>);
    STATIC_REQUIRE(SerializableEvent<OrderRejected>);
    STATIC_REQUIRE(SerializableEvent<OrderCancelled>);
    STATIC_REQUIRE(SerializableEvent<OrderFilled>);
    STATIC_REQUIRE_FALSE(SerializableEvent<std::string>);
}
