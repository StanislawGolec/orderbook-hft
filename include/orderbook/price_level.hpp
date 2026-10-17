#pragma once

#include <cassert>
#include <cstdint>

#include "orderbook/intrusive_list.hpp"
#include "orderbook/order.hpp"
#include "orderbook/price.hpp"
#include "orderbook/types.hpp"

namespace orderbook {

// Represents a price level in the order book aggregating all resting orders at that price
class PriceLevel {
public:
    constexpr PriceLevel() noexcept = default;

    explicit constexpr PriceLevel(Price price) noexcept : price_(price) {}

    [[nodiscard]] constexpr Price price() const noexcept { return price_; }

    constexpr void set_price(Price price) noexcept { price_ = price; }

    [[nodiscard]] constexpr Volume total_volume() const noexcept { return total_volume_; }

    [[nodiscard]] constexpr uint32_t order_count() const noexcept { return order_count_; }

    [[nodiscard]] constexpr bool empty() const noexcept { return order_count_ == 0; }

    [[nodiscard]] IntrusiveList<Order>& orders() noexcept { return orders_; }

    [[nodiscard]] const IntrusiveList<Order>& orders() const noexcept { return orders_; }

    // Appends an order to the price level queue in O(1)
    void add_order(Order* order) noexcept {
        assert(order != nullptr);
        orders_.push_back(order);
        total_volume_ += order->remaining_volume;
        ++order_count_;
    }

    // Removes an order from the price level queue in O(1)
    void remove_order(Order* order) noexcept {
        assert(order != nullptr);
        assert(order_count_ > 0);
        if (total_volume_ >= order->remaining_volume) {
            total_volume_ -= order->remaining_volume;
        } else {
            total_volume_ = 0;
        }
        --order_count_;
        orders_.erase(order);
    }

    // Reduces volume when an order is partially executed or volume is amended down
    void reduce_volume(Volume filled_qty) noexcept {
        assert(total_volume_ >= filled_qty);
        total_volume_ -= filled_qty;
    }

    // Increases volume when an order's volume is amended up
    void increase_volume(Volume add_qty) noexcept { total_volume_ += add_qty; }

    // Moves an existing order to the tail of the FIFO queue (losing time priority)
    void move_to_tail(Order* order) noexcept {
        assert(order != nullptr);
        orders_.erase(order);
        orders_.push_back(order);
    }

    void clear() noexcept {
        orders_.clear();
        total_volume_ = 0;
        order_count_ = 0;
    }

private:
    Price price_{Price::zero()};
    Volume total_volume_{0};
    uint32_t order_count_{0};
    IntrusiveList<Order> orders_;
};

}  // namespace orderbook
