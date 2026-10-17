#pragma once

#if defined(_MSC_VER) || defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
    defined(_M_IX86)
#include <xmmintrin.h>
#endif

namespace orderbook {

enum class PrefetchLocality {
    None = 0,    // Non-temporal (stream without polluting cache)
    Low = 1,     // L3 cache
    Medium = 2,  // L2 cache
    High = 3     // L1 cache
};

template<typename T>
inline void cpu_prefetch(const T* ptr,
                         PrefetchLocality locality = PrefetchLocality::High) noexcept {
    if (!ptr) {
        return;
    }
#if defined(__GNUC__) || defined(__clang__)
    switch (locality) {
        case PrefetchLocality::High:
            __builtin_prefetch(ptr, 0, 3);
            break;
        case PrefetchLocality::Medium:
            __builtin_prefetch(ptr, 0, 2);
            break;
        case PrefetchLocality::Low:
            __builtin_prefetch(ptr, 0, 1);
            break;
        case PrefetchLocality::None:
            __builtin_prefetch(ptr, 0, 0);
            break;
    }
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    switch (locality) {
        case PrefetchLocality::High:
            _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T0);
            break;
        case PrefetchLocality::Medium:
            _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T1);
            break;
        case PrefetchLocality::Low:
            _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T2);
            break;
        case PrefetchLocality::None:
            _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_NTA);
            break;
    }
#else
    (void)ptr;
    (void)locality;
#endif
}

}  // namespace orderbook
