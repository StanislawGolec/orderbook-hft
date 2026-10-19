#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "orderbook/order.hpp"
#include "orderbook/types.hpp"

namespace orderbook {

// Flat hash map with open addressing, linear probing, and backward-shift deletion for O(1) lookup
class FlatOrderIndex {
public:
    struct Entry {
        OrderId id{INVALID_ORDER_ID};
        Order* order{nullptr};
    };

    static_assert(sizeof(Entry) == 16, "FlatOrderIndex::Entry must be 16 bytes");

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

        size_t idx = hash(id) & mask_;
        size_t start_idx = idx;

        while (table_[idx].id != INVALID_ORDER_ID) {
            if (table_[idx].id == id) {
                table_[idx].order = order;
                return true;
            }
            idx = (idx + 1) & mask_;
            if (idx == start_idx) [[unlikely]] {
                return false;
            }
        }

        if (size_ * 2 >= capacity_) [[unlikely]] {
            return false;
        }

        table_[idx].id = id;
        table_[idx].order = order;
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

        while (table_[idx].id != INVALID_ORDER_ID) {
            if (table_[idx].id == id) {
                return table_[idx].order;
            }
            idx = (idx + 1) & mask_;
            if (idx == start_idx) [[unlikely]] {
                break;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool contains(OrderId id) const noexcept { return find(id) != nullptr; }

    // Erases order by ID in O(1) using backward-shift deletion (zero tombstones)
    bool erase(OrderId id) noexcept {
        if (size_ == 0 || id == INVALID_ORDER_ID) {
            return false;
        }

        size_t i = hash(id) & mask_;
        size_t start_idx = i;

        while (table_[i].id != INVALID_ORDER_ID) {
            if (table_[i].id == id) {
                size_t j = i;
                while (true) {
                    j = (j + 1) & mask_;
                    if (table_[j].id == INVALID_ORDER_ID) {
                        break;
                    }
                    size_t k = hash(table_[j].id) & mask_;
                    if (((j - k) & mask_) >= ((j - i) & mask_)) {
                        table_[i] = table_[j];
                        i = j;
                    }
                }
                table_[i].id = INVALID_ORDER_ID;
                table_[i].order = nullptr;
                --size_;
                return true;
            }
            i = (i + 1) & mask_;
            if (i == start_idx) [[unlikely]] {
                break;
            }
        }
        return false;
    }

    void clear() noexcept {
        for (auto& entry : table_) {
            entry.id = INVALID_ORDER_ID;
            entry.order = nullptr;
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
