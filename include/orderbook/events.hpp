#pragma once

#include <cstdint>
#include <type_traits>

#include "orderbook/price.hpp"
#include "orderbook/types.hpp"

namespace orderbook {

// Emitted when two orders match and a trade executes
struct TradeEvent {
    uint64_t match_id{0};
    OrderId maker_order_id{0};
    OrderId taker_order_id{0};
    Price price{Price::zero()};
    Volume quantity{0};
    Side taker_side{Side::Buy};
    Timestamp timestamp{0};
};

// Emitted when an order passes validation and enters the engine
struct OrderAccepted {
    OrderId order_id{0};
    Price price{Price::zero()};
    Volume quantity{0};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    Timestamp timestamp{0};
};

// Emitted when an order rests in the order book queue
struct OrderRested {
    OrderId order_id{0};
    Price price{Price::zero()};
    Volume quantity{0};
    Side side{Side::Buy};
    Timestamp timestamp{0};
};

// Emitted when an order is rejected (invalid params, crossed FOK, etc.)
struct OrderRejected {
    OrderId order_id{0};
    RejectReason reason{RejectReason::None};
    Timestamp timestamp{0};
};

// Emitted when a resting order is cancelled
struct OrderCancelled {
    OrderId order_id{0};
    Price price{Price::zero()};
    Volume remaining_quantity{0};
    Side side{Side::Buy};
    Timestamp timestamp{0};
};

// Emitted when an order is partially or fully filled
struct OrderFilled {
    OrderId order_id{0};
    Price price{Price::zero()};
    Volume filled_quantity{0};
    Volume remaining_quantity{0};
    Timestamp timestamp{0};
};

// Compile-time checks enforcing zero serialization overhead
static_assert(std::is_trivially_copyable_v<TradeEvent>, "TradeEvent must be trivially copyable");
static_assert(std::is_standard_layout_v<TradeEvent>, "TradeEvent must have standard layout");

static_assert(std::is_trivially_copyable_v<OrderAccepted>,
              "OrderAccepted must be trivially copyable");
static_assert(std::is_standard_layout_v<OrderAccepted>, "OrderAccepted must have standard layout");

static_assert(std::is_trivially_copyable_v<OrderRested>, "OrderRested must be trivially copyable");
static_assert(std::is_standard_layout_v<OrderRested>, "OrderRested must have standard layout");

static_assert(std::is_trivially_copyable_v<OrderRejected>,
              "OrderRejected must be trivially copyable");
static_assert(std::is_standard_layout_v<OrderRejected>, "OrderRejected must have standard layout");

static_assert(std::is_trivially_copyable_v<OrderCancelled>,
              "OrderCancelled must be trivially copyable");
static_assert(std::is_standard_layout_v<OrderCancelled>,
              "OrderCancelled must have standard layout");

static_assert(std::is_trivially_copyable_v<OrderFilled>, "OrderFilled must be trivially copyable");
static_assert(std::is_standard_layout_v<OrderFilled>, "OrderFilled must have standard layout");

}  // namespace orderbook
