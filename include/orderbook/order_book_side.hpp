#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <vector>

#include "orderbook/memory_pool.hpp"
#include "orderbook/order.hpp"
#include "orderbook/price.hpp"
#include "orderbook/price_level.hpp"
#include "orderbook/types.hpp"

namespace orderbook {

struct LevelDepth {
    Price price{Price::zero()};
    Volume volume{0};
    uint32_t order_count{0};
};

// Represents one side of the limit order book (Bids or Asks) maintaining sorted price levels
// and an O(1) flat price-to-level index with backward-shift deletion.
class OrderBookSide {
public:
    OrderBookSide(Side side, ObjectPool<PriceLevel>& level_pool, size_t initial_capacity = 128);

    [[nodiscard]] Side side() const noexcept { return side_; }

    [[nodiscard]] bool empty() const noexcept { return levels_.empty(); }

    [[nodiscard]] size_t level_count() const noexcept { return levels_.size(); }

    [[nodiscard]] Volume total_volume() const noexcept { return total_volume_; }

    [[nodiscard]] const std::vector<PriceLevel*>& levels() const noexcept { return levels_; }

    [[nodiscard]] PriceLevel* best_level() noexcept { return best_level_; }

    [[nodiscard]] const PriceLevel* best_level() const noexcept { return best_level_; }

    [[nodiscard]] Price best_price() const noexcept {
        return best_level_ ? best_level_->price() : Price::zero();
    }

    [[nodiscard]] Volume best_volume() const noexcept {
        return best_level_ ? best_level_->total_volume() : 0;
    }

    // Lock-free cross-thread BBO readers
    [[nodiscard]] Price best_price_atomic() const noexcept {
        return Price::from_raw(atomic_best_price_raw_.load(std::memory_order_relaxed));
    }

    [[nodiscard]] Volume best_volume_atomic() const noexcept {
        return atomic_best_volume_.load(std::memory_order_relaxed);
    }

    // Inserts order into its corresponding price level (O(1) if level exists)
    PriceLevel* add_order(Order* order);

    // Removes order in O(1) via level_pool_idx and cleans up empty price level
    bool remove_order(Order* order);

    // Updates volume upon partial fill in O(1) via level_pool_idx
    void reduce_order_volume(Order* order, Volume filled_qty);

    // Modifies volume in-place in O(1); if volume increases, moves order to tail of level
    bool modify_order_volume(Order* order, Volume new_volume);

    [[nodiscard]] PriceLevel* find_level(Price price) const noexcept;

    // Calculates available liquidity up to a limit price without heap allocation
    [[nodiscard]] Volume available_volume_up_to(Price limit_price) const noexcept;

    void reserve(size_t capacity) { levels_.reserve(capacity); }

    // Returns top N market depth levels
    [[nodiscard]] std::vector<LevelDepth> get_depth(size_t max_levels = 10) const;

    void clear() noexcept;

private:
    [[nodiscard]] bool is_better_price(Price a, Price b) const noexcept {
        return side_ == Side::Buy ? (a > b) : (a < b);
    }

    void sync_bbo_atomic() noexcept {
        if (best_level_) {
            atomic_best_price_raw_.store(best_level_->price().raw(), std::memory_order_relaxed);
            atomic_best_volume_.store(best_level_->total_volume(), std::memory_order_relaxed);
        } else {
            atomic_best_price_raw_.store(0, std::memory_order_relaxed);
            atomic_best_volume_.store(0, std::memory_order_relaxed);
        }
    }

    Side side_;
    ObjectPool<PriceLevel>& level_pool_;
    std::vector<PriceLevel*> levels_;
    PriceLevel* best_level_{nullptr};
    Volume total_volume_{0};
    std::atomic<int64_t> atomic_best_price_raw_{0};
    std::atomic<Volume> atomic_best_volume_{0};
};

}  // namespace orderbook
