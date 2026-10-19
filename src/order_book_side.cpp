#include "orderbook/order_book_side.hpp"

#include <algorithm>

namespace orderbook {

OrderBookSide::OrderBookSide(Side side, ObjectPool<PriceLevel>& level_pool, size_t initial_capacity)
    : side_(side), level_pool_(level_pool) {
    levels_.reserve(initial_capacity);
}

PriceLevel* OrderBookSide::find_level(Price price) const noexcept {
    if (best_level_ && best_level_->price() == price) [[likely]] {
        return best_level_;
    }

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

    if (best_level_ && best_level_->price() == order->price) [[likely]] {
        best_level_->add_order(order);
        order->level_pool_idx = level_pool_.index_of(best_level_);
        total_volume_ += order->remaining_volume;
        atomic_best_volume_.store(best_level_->total_volume(), std::memory_order_relaxed);
        return best_level_;
    }

    auto it = std::lower_bound(levels_.begin(), levels_.end(), order->price,
                               [this](const PriceLevel* level, Price target_price) {
                                   return is_better_price(level->price(), target_price);
                               });

    PriceLevel* target_level = nullptr;
    if (it != levels_.end() && (*it)->price() == order->price) [[likely]] {
        target_level = *it;
        target_level->add_order(order);
        order->level_pool_idx = level_pool_.index_of(target_level);
    } else {
        target_level = level_pool_.construct(order->price);
        assert(target_level != nullptr);
        target_level->add_order(order);
        order->level_pool_idx = level_pool_.index_of(target_level);
        levels_.insert(it, target_level);
    }

    best_level_ = levels_.empty() ? nullptr : levels_[0];
    total_volume_ += order->remaining_volume;
    sync_bbo_atomic();
    return target_level;
}

bool OrderBookSide::remove_order(Order* order) {
    if (!order) {
        return false;
    }

    PriceLevel* level = nullptr;
    if (order->level_pool_idx != INVALID_LEVEL_IDX) [[likely]] {
        level = level_pool_.at(order->level_pool_idx);
        if (level && (level->empty() || level->price() != order->price)) [[unlikely]] {
            level = nullptr;
        }
    }
    if (!level) [[unlikely]] {
        level = find_level(order->price);
    }
    if (!level) {
        return false;
    }

    total_volume_ -= order->remaining_volume;
    level->remove_order(order);
    order->level_pool_idx = INVALID_LEVEL_IDX;

    // Fast O(1) path: price level still has remaining orders (no vector search or shift!)
    if (!level->empty()) [[likely]] {
        if (level == best_level_) {
            atomic_best_volume_.store(best_level_->total_volume(), std::memory_order_relaxed);
        }
        return true;
    }

    // Price level became empty: remove from sorted vector and return to pool
    if (level == best_level_) {
        levels_.erase(levels_.begin());
    } else {
        auto it = std::lower_bound(levels_.begin(), levels_.end(), order->price,
                                   [this](const PriceLevel* lvl, Price target_price) {
                                       return is_better_price(lvl->price(), target_price);
                                   });
        if (it != levels_.end() && *it == level) {
            levels_.erase(it);
        }
    }
    level_pool_.destroy(level);

    best_level_ = levels_.empty() ? nullptr : levels_[0];
    sync_bbo_atomic();
    return true;
}

void OrderBookSide::reduce_order_volume(Order* order, Volume filled_qty) {
    if (!order || filled_qty == 0) {
        return;
    }

    PriceLevel* level = (order->level_pool_idx != INVALID_LEVEL_IDX)
                            ? level_pool_.at(order->level_pool_idx)
                            : find_level(order->price);
    if (level) [[likely]] {
        level->reduce_volume(filled_qty);
        total_volume_ -= filled_qty;
        if (level == best_level_) {
            atomic_best_volume_.store(best_level_->total_volume(), std::memory_order_relaxed);
        }
    }
    order->fill(filled_qty);
}

bool OrderBookSide::modify_order_volume(Order* order, Volume new_volume) {
    if (!order || new_volume == 0) {
        return false;
    }

    PriceLevel* level = (order->level_pool_idx != INVALID_LEVEL_IDX)
                            ? level_pool_.at(order->level_pool_idx)
                            : find_level(order->price);
    if (!level || level->empty() || level->price() != order->price) [[unlikely]] {
        return false;
    }

    if (new_volume < order->remaining_volume) {
        Volume diff = order->remaining_volume - new_volume;
        level->reduce_volume(diff);
        total_volume_ -= diff;
        order->remaining_volume = new_volume;
        if (order->initial_volume >= diff) {
            order->initial_volume -= diff;
        }
    } else if (new_volume > order->remaining_volume) {
        Volume diff = new_volume - order->remaining_volume;
        level->increase_volume(diff);
        total_volume_ += diff;
        order->remaining_volume = new_volume;
        order->initial_volume += diff;
        level->move_to_tail(order);
    }

    if (level == best_level_) {
        atomic_best_volume_.store(best_level_->total_volume(), std::memory_order_relaxed);
    }
    return true;
}

Volume OrderBookSide::available_volume_up_to(Price limit_price) const noexcept {
    Volume vol = 0;
    for (const PriceLevel* level : levels_) {
        if (side_ == Side::Buy && level->price() < limit_price) {
            break;
        }
        if (side_ == Side::Sell && level->price() > limit_price) {
            break;
        }
        vol += level->total_volume();
    }
    return vol;
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
    sync_bbo_atomic();
}

}  // namespace orderbook
