#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "orderbook/memory_pool.hpp"
#include "orderbook/order.hpp"

using namespace orderbook;

TEST_CASE("ObjectPool - Basic Acquisition and Release", "[memory_pool]") {
    constexpr size_t POOL_CAPACITY = 10;
    ObjectPool<Order> pool(POOL_CAPACITY);

    REQUIRE(pool.capacity() == POOL_CAPACITY);
    REQUIRE(pool.available_count() == POOL_CAPACITY);
    REQUIRE(pool.allocated_count() == 0);
    REQUIRE(pool.is_empty());
    REQUIRE_FALSE(pool.is_full());

    // Acquire 1 order
    Order* o1 = pool.acquire();
    REQUIRE(o1 != nullptr);
    REQUIRE(pool.contains(o1));
    REQUIRE(pool.available_count() == POOL_CAPACITY - 1);
    REQUIRE(pool.allocated_count() == 1);
    REQUIRE_FALSE(pool.is_empty());

    // Release order
    pool.release(o1);
    REQUIRE(pool.available_count() == POOL_CAPACITY);
    REQUIRE(pool.allocated_count() == 0);
    REQUIRE(pool.is_empty());
}

TEST_CASE("ObjectPool - Exhaustion and Capacity Limits", "[memory_pool]") {
    constexpr size_t POOL_CAPACITY = 5;
    ObjectPool<Order> pool(POOL_CAPACITY);

    std::vector<Order*> allocated;
    for (size_t i = 0; i < POOL_CAPACITY; ++i) {
        Order* o = pool.acquire();
        REQUIRE(o != nullptr);
        allocated.push_back(o);
    }

    REQUIRE(pool.is_full());
    REQUIRE(pool.available_count() == 0);
    REQUIRE(pool.allocated_count() == POOL_CAPACITY);

    // Further acquisition fails cleanly with nullptr
    REQUIRE(pool.acquire() == nullptr);

    // Release all
    for (Order* o : allocated) {
        pool.release(o);
    }
    REQUIRE(pool.is_empty());
    REQUIRE(pool.available_count() == POOL_CAPACITY);
}

TEST_CASE("ObjectPool - Placement Construct and Destroy", "[memory_pool]") {
    ObjectPool<Order> pool(4);

    Order* o = pool.construct(Order{.id = 555,
                                    .price = Price::from_double(120.0),
                                    .initial_volume = 50,
                                    .remaining_volume = 50,
                                    .timestamp = 9999});

    REQUIRE(o != nullptr);
    REQUIRE(o->id == 555);
    REQUIRE(o->price == Price::from_double(120.0));
    REQUIRE(o->remaining_volume == 50);

    pool.destroy(o);
    REQUIRE(pool.is_empty());
}

TEST_CASE("ObjectPool - Reset and High Volume Cycle", "[memory_pool]") {
    constexpr size_t POOL_CAPACITY = 1000;
    ObjectPool<Order> pool(POOL_CAPACITY);

    for (int cycle = 0; cycle < 50; ++cycle) {
        std::vector<Order*> orders;
        orders.reserve(POOL_CAPACITY);
        for (size_t i = 0; i < POOL_CAPACITY; ++i) {
            Order* o = pool.acquire();
            o->id = i;
            orders.push_back(o);
        }
        REQUIRE(pool.is_full());

        for (Order* o : orders) {
            pool.release(o);
        }
        REQUIRE(pool.is_empty());
    }
}

TEST_CASE("StaticArenaPool - Compile-time Fixed Arena", "[memory_pool]") {
    constexpr size_t ARENA_SIZE = 8;
    StaticArenaPool<Order, ARENA_SIZE> arena;

    REQUIRE(StaticArenaPool<Order, ARENA_SIZE>::capacity() == ARENA_SIZE);
    REQUIRE(arena.available_count() == ARENA_SIZE);
    REQUIRE(arena.is_empty());

    std::vector<Order*> orders;
    for (size_t i = 0; i < ARENA_SIZE; ++i) {
        Order* o = arena.construct(Order{.id = i + 1});
        REQUIRE(o != nullptr);
        orders.push_back(o);
    }

    REQUIRE(arena.is_full());
    REQUIRE(arena.acquire() == nullptr);

    for (Order* o : orders) {
        arena.destroy(o);
    }
    REQUIRE(arena.is_empty());
}
