#pragma once

#include <concepts>
#include <cstddef>
#include <iterator>

namespace orderbook {

template<typename T>
concept IntrusiveLinkable = requires(T t) {
    { t.next } -> std::same_as<T*&>;
    { t.prev } -> std::same_as<T*&>;
};

// Intrusive doubly linked list providing O(1) push_back, pop_front, and erase
template<IntrusiveLinkable T>
class IntrusiveList {
public:
    constexpr IntrusiveList() noexcept = default;

    IntrusiveList(const IntrusiveList&) = delete;
    IntrusiveList& operator=(const IntrusiveList&) = delete;

    constexpr IntrusiveList(IntrusiveList&& other) noexcept
        : head_(other.head_), tail_(other.tail_), size_(other.size_) {
        other.head_ = nullptr;
        other.tail_ = nullptr;
        other.size_ = 0;
    }

    constexpr IntrusiveList& operator=(IntrusiveList&& other) noexcept {
        if (this != &other) {
            head_ = other.head_;
            tail_ = other.tail_;
            size_ = other.size_;
            other.head_ = nullptr;
            other.tail_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

    [[nodiscard]] constexpr size_t size() const noexcept { return size_; }

    [[nodiscard]] constexpr T* front() noexcept { return head_; }

    [[nodiscard]] constexpr const T* front() const noexcept { return head_; }

    [[nodiscard]] constexpr T* back() noexcept { return tail_; }

    [[nodiscard]] constexpr const T* back() const noexcept { return tail_; }

    // Appends arriving node to the end of the queue in O(1)
    void push_back(T* node) noexcept {
        if (!node) {
            return;
        }
        node->next = nullptr;
        node->prev = tail_;
        if (tail_) {
            tail_->next = node;
        } else {
            head_ = node;
        }
        tail_ = node;
        ++size_;
    }

    // Removes and returns the highest priority node from the head in O(1)
    T* pop_front() noexcept {
        if (!head_) {
            return nullptr;
        }
        T* node = head_;
        head_ = head_->next;
        if (head_) {
            head_->prev = nullptr;
        } else {
            tail_ = nullptr;
        }
        node->next = nullptr;
        node->prev = nullptr;
        --size_;
        return node;
    }

    // Erases a node by direct pointer without list traversal in O(1)
    void erase(T* node) noexcept {
        if (!node) {
            return;
        }
        if (node->prev) {
            node->prev->next = node->next;
        } else {
            head_ = node->next;
        }

        if (node->next) {
            node->next->prev = node->prev;
        } else {
            tail_ = node->prev;
        }

        node->next = nullptr;
        node->prev = nullptr;
        --size_;
    }

    void clear() noexcept {
        T* curr = head_;
        while (curr) {
            T* next = curr->next;
            curr->next = nullptr;
            curr->prev = nullptr;
            curr = next;
        }
        head_ = nullptr;
        tail_ = nullptr;
        size_ = 0;
    }

    class Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        constexpr explicit Iterator(T* node) noexcept : current_(node) {}

        constexpr reference operator*() const noexcept { return *current_; }
        constexpr pointer operator->() const noexcept { return current_; }

        constexpr Iterator& operator++() noexcept {
            if (current_) {
                current_ = current_->next;
            }
            return *this;
        }

        constexpr Iterator operator++(int) noexcept {
            Iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        constexpr bool operator==(const Iterator& other) const noexcept = default;

    private:
        T* current_{nullptr};
    };

    [[nodiscard]] constexpr Iterator begin() noexcept { return Iterator(head_); }
    [[nodiscard]] constexpr Iterator end() noexcept { return Iterator(nullptr); }

private:
    T* head_{nullptr};
    T* tail_{nullptr};
    size_t size_{0};
};

}  // namespace orderbook
