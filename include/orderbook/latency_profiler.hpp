#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif

#include "orderbook/cpu_pause.hpp"

namespace orderbook {

inline uint64_t rdtsc_probe() noexcept {
#if defined(_MSC_VER) || defined(__x86_64__) || defined(__i386__)
    return __rdtsc();
#else
    return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

// Low-overhead HDR latency histogram and statistical profiler
class LatencyProfiler {
public:
    static constexpr size_t MAX_BUCKETS = 10000;  // 0 to 9,999 ns directly indexed

    LatencyProfiler() {
        calibrate_tsc();
        reset();
    }

    void reset() noexcept {
        buckets_.fill(0);
        overflow_count_ = 0;
        sample_count_ = 0;
        min_cycles_ = UINT64_MAX;
        max_cycles_ = 0;
        total_cycles_ = 0;
    }

    // Calibrate TSC cycles per nanosecond against steady_clock
    void calibrate_tsc() noexcept {
        auto start_clock = std::chrono::steady_clock::now();
        uint64_t start_tsc = rdtsc_probe();

        // Busy wait ~10ms for accurate TSC frequency calibration
        auto target = start_clock + std::chrono::milliseconds(10);
        while (std::chrono::steady_clock::now() < target) {
            cpu_pause();
        }

        auto end_clock = std::chrono::steady_clock::now();
        uint64_t end_tsc = rdtsc_probe();

        auto elapsed_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end_clock - start_clock).count();
        if (elapsed_ns > 0 && end_tsc > start_tsc) {
            tsc_per_ns_ =
                static_cast<double>(end_tsc - start_tsc) / static_cast<double>(elapsed_ns);
        } else {
            tsc_per_ns_ = 2.5;  // Default fallback: 2.5 GHz
        }
    }

    // Record latency sample in CPU cycles with zero dynamic heap allocation
    void record_cycles(uint64_t cycles) noexcept {
        ++sample_count_;
        total_cycles_ += cycles;
        if (cycles < min_cycles_) {
            min_cycles_ = cycles;
        }
        if (cycles > max_cycles_) {
            max_cycles_ = cycles;
        }

        uint64_t ns = static_cast<uint64_t>(static_cast<double>(cycles) / tsc_per_ns_);
        if (ns < MAX_BUCKETS) {
            ++buckets_[ns];
        } else {
            ++overflow_count_;
        }
    }

    // Record latency sample directly in nanoseconds with zero dynamic heap allocation
    void record_ns(uint64_t ns) noexcept {
        ++sample_count_;
        uint64_t cycles = static_cast<uint64_t>(static_cast<double>(ns) * tsc_per_ns_);
        total_cycles_ += cycles;
        if (cycles < min_cycles_) {
            min_cycles_ = cycles;
        }
        if (cycles > max_cycles_) {
            max_cycles_ = cycles;
        }

        if (ns < MAX_BUCKETS) {
            ++buckets_[ns];
        } else {
            ++overflow_count_;
        }
    }

    [[nodiscard]] size_t sample_count() const noexcept { return sample_count_; }
    [[nodiscard]] double tsc_frequency_ghz() const noexcept { return tsc_per_ns_; }

    [[nodiscard]] double min_ns() const noexcept {
        return sample_count_ == 0 ? 0.0 : static_cast<double>(min_cycles_) / tsc_per_ns_;
    }

    [[nodiscard]] double max_ns() const noexcept {
        return sample_count_ == 0 ? 0.0 : static_cast<double>(max_cycles_) / tsc_per_ns_;
    }

    [[nodiscard]] double mean_ns() const noexcept {
        return sample_count_ == 0
                   ? 0.0
                   : (static_cast<double>(total_cycles_) / static_cast<double>(sample_count_)) /
                         tsc_per_ns_;
    }

    // Calculates latency percentile (p50, p90, p95, p99, p99.9, p99.99)
    [[nodiscard]] double percentile_ns(double p) const noexcept {
        if (sample_count_ == 0) {
            return 0.0;
        }
        uint64_t target_count =
            static_cast<uint64_t>(std::ceil(p * static_cast<double>(sample_count_)));
        uint64_t cumulative = 0;

        for (size_t i = 0; i < MAX_BUCKETS; ++i) {
            cumulative += buckets_[i];
            if (cumulative >= target_count) {
                return static_cast<double>(i);
            }
        }
        return max_ns();
    }

    [[nodiscard]] double p50() const noexcept { return percentile_ns(0.50); }
    [[nodiscard]] double p90() const noexcept { return percentile_ns(0.90); }
    [[nodiscard]] double p95() const noexcept { return percentile_ns(0.95); }
    [[nodiscard]] double p99() const noexcept { return percentile_ns(0.99); }
    [[nodiscard]] double p99_9() const noexcept { return percentile_ns(0.999); }
    [[nodiscard]] double p99_99() const noexcept { return percentile_ns(0.9999); }

private:
    std::array<uint64_t, MAX_BUCKETS> buckets_{};
    uint64_t overflow_count_{0};
    size_t sample_count_{0};
    uint64_t min_cycles_{UINT64_MAX};
    uint64_t max_cycles_{0};
    uint64_t total_cycles_{0};
    double tsc_per_ns_{2.5};
};

}  // namespace orderbook
