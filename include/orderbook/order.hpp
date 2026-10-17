#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "orderbook/price.hpp"
#include "orderbook/types.hpp"

namespace orderbook {

// Cache-line aligned Order structure fitting exactly in 64 bytes (1 cache line)
struct alignas(64) Order {
    OrderId id{INVALID_ORDER_ID};
    Price price{Price::zero()};
    Volume initial_volume{0};
    Volume remaining_volume{0};
    Timestamp timestamp{0};
    Order* next{nullptr};
    Order* prev{nullptr};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    TimeInForce tif{TimeInForce::GTC};
    uint8_t pad[5]{0, 0, 0, 0, 0};

    [[nodiscard]] constexpr bool is_filled() const noexcept { return remaining_volume == 0; }

    [[nodiscard]] constexpr Volume filled_volume() const noexcept {
        return initial_volume - remaining_volume;
    }

    constexpr void fill(Volume qty) noexcept {
        if (qty >= remaining_volume) {
            remaining_volume = 0;
        } else {
            remaining_volume -= qty;
        }
    }
};

static_assert(sizeof(Order) == 64, "Order must be exactly 64 bytes");
static_assert(alignof(Order) == 64, "Order must be aligned to 64 bytes");
static_assert(std::is_standard_layout_v<Order>, "Order must have standard layout");

}  // namespace orderbook
