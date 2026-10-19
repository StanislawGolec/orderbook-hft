#include <catch2/catch_test_macros.hpp>
#include <new>

#include "orderbook/alloc_guard.hpp"
#include "orderbook/intrusive_list.hpp"
#include "orderbook/memory_pool.hpp"
#include "orderbook/order.hpp"

using namespace orderbook;

TEST_CASE("AllocationGuard - State Management", "[alloc_guard]") {
    REQUIRE_FALSE(AllocationGuard::is_active());
    REQUIRE_FALSE(g_disallow_allocations);

    bool active_inside = false;
    bool flag_inside = false;
    {
        AllocationGuard guard;
        active_inside = AllocationGuard::is_active();
        flag_inside = g_disallow_allocations;
    }
    REQUIRE(active_inside);
    REQUIRE(flag_inside);

    REQUIRE_FALSE(AllocationGuard::is_active());
    REQUIRE_FALSE(g_disallow_allocations);
}

TEST_CASE("AllocationGuard - Throws BadAlloc On Heap Allocation", "[alloc_guard]") {
    REQUIRE_FALSE(g_disallow_allocations);

    // Dynamic allocation succeeds outside guard
    int* normal_ptr = new int(42);
    REQUIRE(*normal_ptr == 42);
    delete normal_ptr;

    // Inside guard, dynamic allocation throws std::bad_alloc
    bool caught_bad_alloc = false;
    {
        AllocationGuard guard;
        try {
            volatile int* p = new int(100);
            delete p;
        } catch (const std::bad_alloc&) {
            caught_bad_alloc = true;
        }
    }
    REQUIRE(caught_bad_alloc);
}

TEST_CASE("AllocationGuard - Zero Allocation Hot Path Proof", "[alloc_guard]") {
    constexpr size_t CAPACITY = 1000;
    ObjectPool<Order> pool(CAPACITY);
    IntrusiveList<Order> list;

    // Hot path starts: Zero heap allocations permitted
    {
        AllocationGuard guard;

        // Perform order ingestion, list operations, cancellation, and release
        for (size_t i = 0; i < 500; ++i) {
            Order* o = pool.acquire();
            o->id = i;
            o->price = Price::from_double(100.0);
            o->initial_volume = 10;
            o->remaining_volume = 10;
            list.push_back(o);
        }

        // Match head orders
        for (size_t i = 0; i < 250; ++i) {
            Order* o = list.pop_front();
            pool.release(o);
        }

        // Cancel order by direct pointer
        Order* to_cancel = list.front();
        list.erase(to_cancel);
        pool.release(to_cancel);

        // Clear remaining orders
        while (!list.empty()) {
            Order* o = list.pop_front();
            pool.release(o);
        }
    }

    // Verify system state after hot-path execution
    REQUIRE(list.empty());
    REQUIRE(pool.is_empty());
    REQUIRE(pool.available_count() == CAPACITY);
}
