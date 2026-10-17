#include "orderbook/order_book.hpp"

namespace orderbook {

OrderBook::OrderBook(size_t max_orders, size_t max_levels)
    : order_pool_(max_orders), level_pool_(max_levels), order_index_(max_orders),
      bids_(Side::Buy, level_pool_, max_levels), asks_(Side::Sell, level_pool_, max_levels) {}

void OrderBook::preallocate(size_t max_orders, size_t max_levels) {
    order_pool_.init(max_orders);
    level_pool_.init(max_levels);
    order_index_.init(max_orders);
    bids_.clear();
    asks_.clear();
    bids_.reserve(max_levels);
    asks_.reserve(max_levels);
}

Order* OrderBook::add_limit_order(OrderId id, Side side, Price price, Volume volume, Timestamp ts,
                                  TimeInForce tif, Volume initial_volume) {
    if (id == INVALID_ORDER_ID || volume == 0 || order_index_.contains(id)) {
        return nullptr;
    }

    Volume init_vol = (initial_volume > 0) ? initial_volume : volume;

    Order* order = order_pool_.construct(Order{.id = id,
                                               .price = price,
                                               .initial_volume = init_vol,
                                               .remaining_volume = volume,
                                               .timestamp = ts,
                                               .side = side,
                                               .type = OrderType::Limit,
                                               .tif = tif});

    if (!order) {
        return nullptr;
    }

    if (!order_index_.insert(id, order)) {
        order_pool_.destroy(order);
        return nullptr;
    }

    if (side == Side::Buy) {
        bids_.add_order(order);
    } else {
        asks_.add_order(order);
    }

    return order;
}

bool OrderBook::cancel_order(OrderId id) {
    Order* order = order_index_.find(id);
    if (!order) {
        return false;
    }

    if (order->side == Side::Buy) {
        bids_.remove_order(order);
    } else {
        asks_.remove_order(order);
    }

    order_index_.erase(id);
    order_pool_.destroy(order);
    return true;
}

void OrderBook::remove_filled_order(Order* order) {
    if (!order) {
        return;
    }

    OrderId id = order->id;
    if (order->side == Side::Buy) {
        bids_.remove_order(order);
    } else {
        asks_.remove_order(order);
    }

    order_index_.erase(id);
    order_pool_.destroy(order);
}

void OrderBook::reduce_order_volume(Order* order, Volume filled_qty) {
    if (!order || filled_qty == 0) {
        return;
    }

    if (order->side == Side::Buy) {
        bids_.reduce_order_volume(order, filled_qty);
    } else {
        asks_.reduce_order_volume(order, filled_qty);
    }
}

bool OrderBook::modify_order_volume(Order* order, Volume new_volume) {
    if (!order || new_volume == 0) {
        return false;
    }

    if (order->side == Side::Buy) {
        return bids_.modify_order_volume(order, new_volume);
    } else {
        return asks_.modify_order_volume(order, new_volume);
    }
}

Order* OrderBook::find_order(OrderId id) const noexcept {
    return order_index_.find(id);
}

Price OrderBook::best_bid() const noexcept {
    return bids_.best_price();
}

Price OrderBook::best_ask() const noexcept {
    return asks_.best_price();
}

Volume OrderBook::best_bid_volume() const noexcept {
    return bids_.best_volume();
}

Volume OrderBook::best_ask_volume() const noexcept {
    return asks_.best_volume();
}

bool OrderBook::has_bids() const noexcept {
    return !bids_.empty();
}

bool OrderBook::has_asks() const noexcept {
    return !asks_.empty();
}

bool OrderBook::is_empty() const noexcept {
    return bids_.empty() && asks_.empty();
}

size_t OrderBook::order_count() const noexcept {
    return order_index_.size();
}

std::vector<LevelDepth> OrderBook::get_bids_depth(size_t max_levels) const {
    return bids_.get_depth(max_levels);
}

std::vector<LevelDepth> OrderBook::get_asks_depth(size_t max_levels) const {
    return asks_.get_depth(max_levels);
}

void OrderBook::clear() noexcept {
    bids_.clear();
    asks_.clear();
    order_index_.clear();
    order_pool_.reset();
    level_pool_.reset();
}

}  // namespace orderbook
