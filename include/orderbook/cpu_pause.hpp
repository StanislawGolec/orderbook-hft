#pragma once

#if defined(_MSC_VER) || defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
    defined(_M_IX86)
#include <immintrin.h>
#endif

#if !defined(_MSC_VER) && (defined(__ARM_NEON) || defined(__aarch64__))
#include <arm_neon.h>
#endif

#include <thread>

namespace orderbook {

// Emits CPU pause/yield instruction to prevent pipeline stalls and excessive power in spin loops
inline void cpu_pause() noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    _mm_pause();
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
    _mm_pause();
#elif defined(__aarch64__) || defined(__ARM_ARCH_8A__)
    __asm__ __volatile__("yield" ::: "memory");
#elif defined(_MSC_VER) && defined(_M_ARM64)
    __yield();
#else
    std::this_thread::yield();
#endif
}

}  // namespace orderbook
