#pragma once

#include <cstddef>
#include <vector>

#include "orderbook/memory_pool.hpp"
#include "orderbook/order.hpp"
#include "orderbook/order_book_side.hpp"
#include "orderbook/order_index.hpp"
#include "orderbook/price.hpp"
#include "orderbook/price_level.hpp"
#include "orderbook/types.hpp"

namespace orderbook {

// Core Limit Order Book coordinating bids, asks, fast order indexing, and zero-allocation pools
class OrderBook {
public:
    explicit OrderBook(size_t max_orders = 100000, size_t max_levels = 10000);

    // Warm up and pre-allocate pools and hash index
    void preallocate(size_t max_orders, size_t max_levels = 10000);

    // Adds a resting limit order to the book and index
    Order* add_limit_order(OrderId id, Side side, Price price, Volume volume, Timestamp ts,
                           TimeInForce tif = TimeInForce::GTC);

    // Cancels an existing order in O(1) by OrderId
    bool cancel_order(OrderId id);

    [[nodiscard]] Order* find_order(OrderId id) const noexcept;

    // Best Bid / Best Offer (BBO) queries
    [[nodiscard]] Price best_bid() const noexcept;
    [[nodiscard]] Price best_ask() const noexcept;
    [[nodiscard]] Volume best_bid_volume() const noexcept;
    [[nodiscard]] Volume best_ask_volume() const noexcept;

    [[nodiscard]] bool has_bids() const noexcept;
    [[nodiscard]] bool has_asks() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept;
    [[nodiscard]] size_t order_count() const noexcept;

    [[nodiscard]] OrderBookSide& bids() noexcept { return bids_; }
    [[nodiscard]] const OrderBookSide& bids() const noexcept { return bids_; }
    [[nodiscard]] OrderBookSide& asks() noexcept { return asks_; }
    [[nodiscard]] const OrderBookSide& asks() const noexcept { return asks_; }

    [[nodiscard]] ObjectPool<Order>& order_pool() noexcept { return order_pool_; }

    // Market depth snapshots
    [[nodiscard]] std::vector<LevelDepth> get_bids_depth(size_t max_levels = 10) const;
    [[nodiscard]] std::vector<LevelDepth> get_asks_depth(size_t max_levels = 10) const;

    void clear() noexcept;

private:
    ObjectPool<Order> order_pool_;
    ObjectPool<PriceLevel> level_pool_;
    FlatOrderIndex order_index_;
    OrderBookSide bids_;
    OrderBookSide asks_;
};

}  // namespace orderbook
