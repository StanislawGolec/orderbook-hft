#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // Structure was padded due to alignment specifier
#include <malloc.h>
#endif

namespace orderbook {

namespace detail {

inline constexpr size_t next_power_of_two(size_t v) noexcept {
    if (v == 0) {
        return 1;
    }
    --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    return v + 1;
}

inline void* allocate_aligned_memory(size_t bytes, size_t alignment = 64) noexcept {
#if defined(_MSC_VER)
    return _aligned_malloc(bytes, alignment);
#else
    size_t remainder = bytes % alignment;
    if (remainder != 0) {
        bytes += (alignment - remainder);
    }
    return std::aligned_alloc(alignment, bytes);
#endif
}

inline void free_aligned_memory(void* ptr) noexcept {
#if defined(_MSC_VER)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

}  // namespace detail

// Lock-Free Bounded Single-Producer Single-Consumer (SPSC) Ring Buffer
// Eliminates false sharing via explicit 64-byte cache line alignment and local sequence caching.
template<typename T>
class SPSCQueue {
public:
    static_assert(std::is_nothrow_destructible_v<T>, "T must be nothrow destructible");

    static constexpr size_t CACHE_LINE_SIZE = 64;

    explicit SPSCQueue(size_t requested_capacity = 65536)
        : capacity_(detail::next_power_of_two(requested_capacity < 2 ? 2 : requested_capacity)),
          mask_(capacity_ - 1) {
        ring_ = static_cast<T*>(
            detail::allocate_aligned_memory(capacity_ * sizeof(T), CACHE_LINE_SIZE));
        assert(ring_ != nullptr);
    }

    ~SPSCQueue() {
        clear();
        detail::free_aligned_memory(ring_);
        ring_ = nullptr;
    }

    // Non-copyable and non-movable to ensure thread safety
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;

    // Enqueues an element. Called ONLY by producer thread.
    // Returns false if queue is full.
    bool try_push(const T& item) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        if (current_head - cached_tail_ >= capacity_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (current_head - cached_tail_ >= capacity_) {
                return false;
            }
        }

        new (static_cast<void*>(&ring_[current_head & mask_])) T(item);
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    // Enqueues an element via move semantics. Called ONLY by producer thread.
    bool try_push(T&& item) noexcept(std::is_nothrow_move_constructible_v<T>) {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        if (current_head - cached_tail_ >= capacity_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (current_head - cached_tail_ >= capacity_) {
                return false;
            }
        }

        new (static_cast<void*>(&ring_[current_head & mask_])) T(std::move(item));
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    // In-place construct an element in the queue.
    template<typename... Args>
    bool try_emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        if (current_head - cached_tail_ >= capacity_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (current_head - cached_tail_ >= capacity_) {
                return false;
            }
        }

        new (static_cast<void*>(&ring_[current_head & mask_])) T(std::forward<Args>(args)...);
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    // Dequeues an element. Called ONLY by consumer thread.
    // Returns false if queue is empty.
    bool try_pop(T& item) noexcept(std::is_nothrow_move_assignable_v<T>) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);

        if (current_tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (current_tail == cached_head_) {
                return false;
            }
        }

        T* slot = &ring_[current_tail & mask_];
        item = std::move(*slot);
        slot->~T();

        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool full() const noexcept { return size() >= capacity_; }

    [[nodiscard]] size_t size() const noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_relaxed);
        return (head >= tail) ? (head - tail) : 0;
    }

    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }

    void clear() noexcept {
        T item;
        while (try_pop(item)) {
            // Drain elements
        }
    }

private:
    // Producer cache line: modified by producer, tail cached locally
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
    size_t cached_tail_{0};
    uint8_t pad_producer_[CACHE_LINE_SIZE - sizeof(std::atomic<size_t>) - sizeof(size_t)]{};

    // Consumer cache line: modified by consumer, head cached locally
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};
    size_t cached_head_{0};
    uint8_t pad_consumer_[CACHE_LINE_SIZE - sizeof(std::atomic<size_t>) - sizeof(size_t)]{};

    // Buffer metadata and storage pointer
    alignas(CACHE_LINE_SIZE) size_t capacity_{0};
    size_t mask_{0};
    T* ring_{nullptr};
    uint8_t pad_buffer_[CACHE_LINE_SIZE - 2 * sizeof(size_t) - sizeof(T*)]{};
};

}  // namespace orderbook

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
