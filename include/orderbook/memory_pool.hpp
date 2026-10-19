#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>
#include <vector>

namespace orderbook {

// Pre-allocated contiguous memory pool for zero-allocation hot paths
template<typename T>
class ObjectPool {
public:
    explicit ObjectPool(size_t capacity = 0) {
        if (capacity > 0) {
            init(capacity);
        }
    }

    ~ObjectPool() { cleanup(); }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    ObjectPool(ObjectPool&& other) noexcept
        : storage_(other.storage_), free_list_(std::move(other.free_list_)),
          free_top_(other.free_top_), capacity_(other.capacity_) {
        other.storage_ = nullptr;
        other.free_top_ = 0;
        other.capacity_ = 0;
    }

    ObjectPool& operator=(ObjectPool&& other) noexcept {
        if (this != &other) {
            cleanup();
            storage_ = other.storage_;
            free_list_ = std::move(other.free_list_);
            free_top_ = other.free_top_;
            capacity_ = other.capacity_;
            other.storage_ = nullptr;
            other.free_top_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }

    // Preallocates a contiguous block of memory with 64-byte alignment
    void init(size_t capacity) {
        if (capacity_ > 0) {
            cleanup();
        }
        capacity_ = capacity;
        if (capacity == 0) {
            return;
        }

        // Allocate raw aligned memory for objects
        constexpr size_t alignment = alignof(T) > 64 ? alignof(T) : 64;
        size_t total_bytes = capacity * sizeof(T);
        storage_ = static_cast<T*>(::operator new(total_bytes, std::align_val_t{alignment}));

        free_list_.resize(capacity);
        for (size_t i = 0; i < capacity; ++i) {
            free_list_[i] = storage_ + i;
        }
        free_top_ = capacity;
    }

    // O(1) allocation without touching the heap
    [[nodiscard]] T* acquire() noexcept {
        if (free_top_ == 0) {
            return nullptr;
        }
        return free_list_[--free_top_];
    }

    // O(1) deallocation returning object to the pool
    void release(T* ptr) noexcept {
        assert(ptr != nullptr);
        assert(contains(ptr));
        assert(free_top_ < capacity_);
        free_list_[free_top_++] = ptr;
    }

    template<typename... Args>
    [[nodiscard]] T* construct(Args&&... args) {
        T* ptr = acquire();
        if (!ptr) {
            return nullptr;
        }
        return ::new (static_cast<void*>(ptr)) T(std::forward<Args>(args)...);
    }

    void destroy(T* ptr) noexcept {
        if (!ptr) {
            return;
        }
        ptr->~T();
        release(ptr);
    }

    // Resets free list to make all slots available without releasing buffer
    void reset() noexcept {
        for (size_t i = 0; i < capacity_; ++i) {
            free_list_[i] = storage_ + i;
        }
        free_top_ = capacity_;
    }

    [[nodiscard]] bool contains(const T* ptr) const noexcept {
        return ptr >= storage_ && ptr < (storage_ + capacity_);
    }

    // Returns 32-bit pool index for O(1) compact pointer compression
    [[nodiscard]] uint32_t index_of(const T* ptr) const noexcept {
        assert(contains(ptr));
        return static_cast<uint32_t>(ptr - storage_);
    }

    // Resolves 32-bit pool index back to object pointer in O(1)
    [[nodiscard]] T* at(uint32_t idx) noexcept {
        if (idx >= capacity_) [[unlikely]] {
            return nullptr;
        }
        return storage_ + idx;
    }

    [[nodiscard]] const T* at(uint32_t idx) const noexcept {
        if (idx >= capacity_) [[unlikely]] {
            return nullptr;
        }
        return storage_ + idx;
    }

    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }

    [[nodiscard]] size_t available_count() const noexcept { return free_top_; }

    [[nodiscard]] size_t allocated_count() const noexcept { return capacity_ - free_top_; }

    [[nodiscard]] bool is_full() const noexcept { return free_top_ == 0; }

    [[nodiscard]] bool is_empty() const noexcept { return free_top_ == capacity_; }

private:
    void cleanup() noexcept {
        if (storage_) {
            constexpr size_t alignment = alignof(T) > 64 ? alignof(T) : 64;
            ::operator delete(storage_, std::align_val_t{alignment});
            storage_ = nullptr;
        }
        free_list_.clear();
        free_top_ = 0;
        capacity_ = 0;
    }

    T* storage_{nullptr};
    std::vector<T*> free_list_;
    size_t free_top_{0};
    size_t capacity_{0};
};

// Fixed compile-time capacity static memory arena
template<typename T, size_t Capacity>
class StaticArenaPool {
public:
    StaticArenaPool() noexcept {
        for (size_t i = 0; i < Capacity; ++i) {
            free_list_[i] = reinterpret_cast<T*>(&storage_[i]);
        }
        free_top_ = Capacity;
    }

    ~StaticArenaPool() = default;

    StaticArenaPool(const StaticArenaPool&) = delete;
    StaticArenaPool& operator=(const StaticArenaPool&) = delete;

    [[nodiscard]] T* acquire() noexcept {
        if (free_top_ == 0) {
            return nullptr;
        }
        return free_list_[--free_top_];
    }

    void release(T* ptr) noexcept {
        assert(ptr != nullptr);
        assert(free_top_ < Capacity);
        free_list_[free_top_++] = ptr;
    }

    template<typename... Args>
    [[nodiscard]] T* construct(Args&&... args) {
        T* ptr = acquire();
        if (!ptr) {
            return nullptr;
        }
        return ::new (static_cast<void*>(ptr)) T(std::forward<Args>(args)...);
    }

    void destroy(T* ptr) noexcept {
        if (!ptr) {
            return;
        }
        ptr->~T();
        release(ptr);
    }

    [[nodiscard]] static constexpr size_t capacity() noexcept { return Capacity; }

    [[nodiscard]] size_t available_count() const noexcept { return free_top_; }

    [[nodiscard]] size_t allocated_count() const noexcept { return Capacity - free_top_; }

    [[nodiscard]] bool is_full() const noexcept { return free_top_ == 0; }

    [[nodiscard]] bool is_empty() const noexcept { return free_top_ == Capacity; }

private:
    struct alignas(alignof(T) > 64 ? alignof(T) : 64) StorageSlot {
        uint8_t data[sizeof(T)];
    };

    StorageSlot storage_[Capacity];
    T* free_list_[Capacity];
    size_t free_top_{Capacity};
};

}  // namespace orderbook
