#include "orderbook/alloc_guard.hpp"

#include <cstdlib>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace orderbook {
thread_local bool g_disallow_allocations = false;
}  // namespace orderbook

namespace {
inline void* aligned_alloc_wrapper(std::size_t size, std::size_t alignment) {
#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return nullptr;
    }
    return ptr;
#endif
}

inline void aligned_free_wrapper(void* ptr) noexcept {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}
}  // namespace

void* operator new(std::size_t size) {
    if (orderbook::g_disallow_allocations) {
        throw std::bad_alloc();
    }
    void* ptr = std::malloc(size);
    if (!ptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

void* operator new[](std::size_t size) {
    if (orderbook::g_disallow_allocations) {
        throw std::bad_alloc();
    }
    void* ptr = std::malloc(size);
    if (!ptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

void operator delete[](void* ptr) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

void* operator new(std::size_t size, std::align_val_t al) {
    if (orderbook::g_disallow_allocations) {
        throw std::bad_alloc();
    }
    void* ptr = aligned_alloc_wrapper(size, static_cast<std::size_t>(al));
    if (!ptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

void operator delete(void* ptr, std::align_val_t) noexcept {
    aligned_free_wrapper(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
    aligned_free_wrapper(ptr);
}

void* operator new[](std::size_t size, std::align_val_t al) {
    if (orderbook::g_disallow_allocations) {
        throw std::bad_alloc();
    }
    void* ptr = aligned_alloc_wrapper(size, static_cast<std::size_t>(al));
    if (!ptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
    aligned_free_wrapper(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
    aligned_free_wrapper(ptr);
}
