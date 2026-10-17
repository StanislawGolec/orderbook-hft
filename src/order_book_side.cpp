#include "orderbook/order_book_side.hpp"

#include <algorithm>

namespace orderbook {

OrderBookSide::OrderBookSide(Side side, ObjectPool<PriceLevel>& level_pool, size_t initial_capacity)
    : side_(side), level_pool_(level_pool) {
    levels_.reserve(initial_capacity);
}

PriceLevel* OrderBookSide::find_level(Price price) const noexcept {
    auto it = std::lower_bound(levels_.begin(), levels_.end(), price,
                               [this](const PriceLevel* level, Price target_price) {
                                   return is_better_price(level->price(), target_price);
                               });

    if (it != levels_.end() && (*it)->price() == price) {
        return *it;
    }
    return nullptr;
}

PriceLevel* OrderBookSide::add_order(Order* order) {
    assert(order != nullptr);

    auto it = std::lower_bound(levels_.begin(), levels_.end(), order->price,
                               [this](const PriceLevel* level, Price target_price) {
                                   return is_better_price(level->price(), target_price);
                               });

    PriceLevel* target_level = nullptr;
    if (it != levels_.end() && (*it)->price() == order->price) {
        target_level = *it;
        target_level->add_order(order);
    } else {
        target_level = level_pool_.construct(order->price);
        assert(target_level != nullptr);
        target_level->add_order(order);
        levels_.insert(it, target_level);
    }

    best_level_ = levels_.empty() ? nullptr : levels_[0];
    total_volume_ += order->remaining_volume;
    return target_level;
}

bool OrderBookSide::remove_order(Order* order) {
    if (!order) {
        return false;
    }

    auto it = std::lower_bound(levels_.begin(), levels_.end(), order->price,
                               [this](const PriceLevel* level, Price target_price) {
                                   return is_better_price(level->price(), target_price);
                               });

    if (it == levels_.end() || (*it)->price() != order->price) {
        return false;
    }

    PriceLevel* level = *it;
    level->remove_order(order);
    total_volume_ -= order->remaining_volume;

    if (level->empty()) {
        levels_.erase(it);
        level_pool_.destroy(level);
    }

    best_level_ = levels_.empty() ? nullptr : levels_[0];
    return true;
}

void OrderBookSide::reduce_order_volume(Order* order, Volume filled_qty) {
    if (!order || filled_qty == 0) {
        return;
    }

    PriceLevel* level = find_level(order->price);
    if (level) {
        level->reduce_volume(filled_qty);
        total_volume_ -= filled_qty;
    }
}

std::vector<LevelDepth> OrderBookSide::get_depth(size_t max_levels) const {
    size_t count = std::min(max_levels, levels_.size());
    std::vector<LevelDepth> depth;
    depth.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        depth.push_back(LevelDepth{.price = levels_[i]->price(),
                                   .volume = levels_[i]->total_volume(),
                                   .order_count = levels_[i]->order_count()});
    }
    return depth;
}

void OrderBookSide::clear() noexcept {
    for (PriceLevel* level : levels_) {
        level->clear();
        level_pool_.destroy(level);
    }
    levels_.clear();
    best_level_ = nullptr;
    total_volume_ = 0;
}

}  // namespace orderbook
