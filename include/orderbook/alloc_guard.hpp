#pragma once

namespace orderbook {

// Global thread-local flag controlling allocation interception on the hot path
extern thread_local bool g_disallow_allocations;

// RAII guard enforcing zero dynamic heap allocations in critical scopes
class AllocationGuard {
public:
    AllocationGuard() noexcept : previous_state_(g_disallow_allocations) {
        g_disallow_allocations = true;
    }

    ~AllocationGuard() noexcept { g_disallow_allocations = previous_state_; }

    AllocationGuard(const AllocationGuard&) = delete;
    AllocationGuard& operator=(const AllocationGuard&) = delete;

    [[nodiscard]] static bool is_active() noexcept { return g_disallow_allocations; }

private:
    bool previous_state_;
};

}  // namespace orderbook
