#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "orderbook/order.hpp"
#include "orderbook/types.hpp"

namespace orderbook {

// Flat hash map with open addressing and linear probing for O(1) order lookup
class FlatOrderIndex {
public:
    struct Entry {
        OrderId id{INVALID_ORDER_ID};
        Order* order{nullptr};
        bool is_tombstone{false};
    };

    explicit FlatOrderIndex(size_t capacity = 1024) { init(capacity); }

    void init(size_t capacity) {
        size_t cap = 16;
        while (cap < capacity * 2) {
            cap <<= 1;
        }
        capacity_ = cap;
        mask_ = cap - 1;
        table_.resize(capacity_);
        clear();
    }

    [[nodiscard]] size_t size() const noexcept { return size_; }

    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    // Inserts or updates an order pointer in O(1)
    bool insert(OrderId id, Order* order) noexcept {
        assert(id != INVALID_ORDER_ID);
        assert(order != nullptr);

        if (size_ * 2 >= capacity_) {
            return false;
        }

        size_t idx = hash(id) & mask_;
        size_t first_tombstone = capacity_;

        while (table_[idx].id != INVALID_ORDER_ID || table_[idx].is_tombstone) {
            if (!table_[idx].is_tombstone && table_[idx].id == id) {
                table_[idx].order = order;
                return true;
            }
            if (table_[idx].is_tombstone && first_tombstone == capacity_) {
                first_tombstone = idx;
            }
            idx = (idx + 1) & mask_;
        }

        size_t target_idx = (first_tombstone != capacity_) ? first_tombstone : idx;
        table_[target_idx].id = id;
        table_[target_idx].order = order;
        table_[target_idx].is_tombstone = false;
        ++size_;
        return true;
    }

    // Retrieves order pointer in O(1)
    [[nodiscard]] Order* find(OrderId id) const noexcept {
        if (size_ == 0 || id == INVALID_ORDER_ID) {
            return nullptr;
        }

        size_t idx = hash(id) & mask_;
        size_t start_idx = idx;

        while (table_[idx].id != INVALID_ORDER_ID || table_[idx].is_tombstone) {
            if (!table_[idx].is_tombstone && table_[idx].id == id) {
                return table_[idx].order;
            }
            idx = (idx + 1) & mask_;
            if (idx == start_idx) {
                break;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool contains(OrderId id) const noexcept { return find(id) != nullptr; }

    // Erases order by ID in O(1)
    bool erase(OrderId id) noexcept {
        if (size_ == 0 || id == INVALID_ORDER_ID) {
            return false;
        }

        size_t idx = hash(id) & mask_;
        size_t start_idx = idx;

        while (table_[idx].id != INVALID_ORDER_ID || table_[idx].is_tombstone) {
            if (!table_[idx].is_tombstone && table_[idx].id == id) {
                table_[idx].is_tombstone = true;
                table_[idx].order = nullptr;
                --size_;
                return true;
            }
            idx = (idx + 1) & mask_;
            if (idx == start_idx) {
                break;
            }
        }
        return false;
    }

    void clear() noexcept {
        for (auto& entry : table_) {
            entry.id = INVALID_ORDER_ID;
            entry.order = nullptr;
            entry.is_tombstone = false;
        }
        size_ = 0;
    }

private:
    static constexpr uint64_t hash(OrderId x) noexcept {
        x = (~x) + (x << 21);
        x = x ^ (x >> 24);
        x = (x + (x << 3)) + (x << 8);
        x = x ^ (x >> 14);
        x = (x + (x << 2)) + (x << 4);
        x = x ^ (x >> 28);
        x = x + (x << 31);
        return x;
    }

    std::vector<Entry> table_;
    size_t capacity_{0};
    size_t mask_{0};
    size_t size_{0};
};

}  // namespace orderbook
