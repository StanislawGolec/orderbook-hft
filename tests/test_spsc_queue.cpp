#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

#include "orderbook/alloc_guard.hpp"
#include "orderbook/cpu_pause.hpp"
#include "orderbook/spsc_queue.hpp"

using namespace orderbook;

TEST_CASE("SPSCQueue - Basic Push and Pop", "[spsc_queue]") {
    SPSCQueue<uint64_t> queue(16);

    REQUIRE(queue.empty());
    REQUIRE(!queue.full());
    REQUIRE(queue.size() == 0);
    REQUIRE(queue.capacity() == 16);

    // Push elements
    for (uint64_t i = 1; i <= 16; ++i) {
        REQUIRE(queue.try_push(i));
    }

    REQUIRE(queue.full());
    REQUIRE(!queue.empty());
    REQUIRE(queue.size() == 16);

    // Cannot push into full queue
    REQUIRE(!queue.try_push(999));

    // Pop elements in FIFO order
    for (uint64_t i = 1; i <= 16; ++i) {
        uint64_t val = 0;
        REQUIRE(queue.try_pop(val));
        REQUIRE(val == i);
    }

    REQUIRE(queue.empty());
    REQUIRE(queue.size() == 0);

    // Cannot pop from empty queue
    uint64_t val = 0;
    REQUIRE(!queue.try_pop(val));
}

TEST_CASE("SPSCQueue - Wrap-around Circular Buffer Mechanics", "[spsc_queue]") {
    SPSCQueue<int> queue(4);
    REQUIRE(queue.capacity() == 4);

    for (int cycle = 0; cycle < 10; ++cycle) {
        REQUIRE(queue.try_push(cycle * 10 + 1));
        REQUIRE(queue.try_push(cycle * 10 + 2));

        int v1 = 0, v2 = 0;
        REQUIRE(queue.try_pop(v1));
        REQUIRE(queue.try_pop(v2));

        REQUIRE(v1 == cycle * 10 + 1);
        REQUIRE(v2 == cycle * 10 + 2);
    }

    REQUIRE(queue.empty());
}

TEST_CASE("SPSCQueue - Multi-threaded Producer-Consumer Stress Test", "[spsc_queue]") {
    constexpr size_t TOTAL_ITEMS = 500000;
    SPSCQueue<uint64_t> queue(8192);

    std::atomic<bool> producer_done{false};
    std::vector<uint64_t> received;
    received.reserve(TOTAL_ITEMS);

    // Producer thread
    std::thread producer([&]() {
        for (uint64_t i = 0; i < TOTAL_ITEMS; ++i) {
            while (!queue.try_push(i)) {
                cpu_pause();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    // Consumer thread
    std::thread consumer([&]() {
        uint64_t val = 0;
        while (!producer_done.load(std::memory_order_acquire) || !queue.empty()) {
            if (queue.try_pop(val)) {
                received.push_back(val);
            } else {
                cpu_pause();
            }
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(received.size() == TOTAL_ITEMS);
    for (size_t i = 0; i < TOTAL_ITEMS; ++i) {
        REQUIRE(received[i] == i);
    }
}

TEST_CASE("SPSCQueue - Cache Line Alignment and Zero False Sharing", "[spsc_queue]") {
    SPSCQueue<uint64_t> queue(1024);

    REQUIRE(alignof(SPSCQueue<uint64_t>) >= 64);
}

TEST_CASE("SPSCQueue - Zero Dynamic Allocation on Hot Path", "[spsc_queue]") {
    SPSCQueue<uint64_t> queue(1024);

    // Hot path starts: Zero allocations allowed
    {
        AllocationGuard guard;

        for (uint64_t i = 0; i < 500; ++i) {
            bool pushed = queue.try_push(i);
            (void)pushed;
        }

        for (uint64_t i = 0; i < 500; ++i) {
            uint64_t val = 0;
            bool popped = queue.try_pop(val);
            (void)popped;
        }
    }

    REQUIRE(queue.empty());
}
