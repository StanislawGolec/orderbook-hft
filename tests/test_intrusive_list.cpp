#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "orderbook/intrusive_list.hpp"
#include "orderbook/order.hpp"

using namespace orderbook;

TEST_CASE("IntrusiveList - Basic Operations and FIFO Order", "[intrusive_list]") {
    IntrusiveList<Order> list;
    REQUIRE(list.empty());
    REQUIRE(list.size() == 0);
    REQUIRE(list.front() == nullptr);
    REQUIRE(list.back() == nullptr);

    Order o1{.id = 1};
    Order o2{.id = 2};
    Order o3{.id = 3};

    // Push back o1
    list.push_back(&o1);
    REQUIRE_FALSE(list.empty());
    REQUIRE(list.size() == 1);
    REQUIRE(list.front() == &o1);
    REQUIRE(list.back() == &o1);

    // Push back o2, o3
    list.push_back(&o2);
    list.push_back(&o3);
    REQUIRE(list.size() == 3);
    REQUIRE(list.front() == &o1);
    REQUIRE(list.back() == &o3);

    // Verify FIFO pop_front
    Order* popped1 = list.pop_front();
    REQUIRE(popped1 == &o1);
    REQUIRE(list.size() == 2);
    REQUIRE(list.front() == &o2);

    Order* popped2 = list.pop_front();
    REQUIRE(popped2 == &o2);
    REQUIRE(list.size() == 1);
    REQUIRE(list.front() == &o3);

    Order* popped3 = list.pop_front();
    REQUIRE(popped3 == &o3);
    REQUIRE(list.empty());
    REQUIRE(list.size() == 0);
    REQUIRE(list.front() == nullptr);
    REQUIRE(list.back() == nullptr);

    // Popping empty returns nullptr
    REQUIRE(list.pop_front() == nullptr);
}

TEST_CASE("IntrusiveList - O(1) Direct Pointer Erase", "[intrusive_list]") {
    Order o1{.id = 1};
    Order o2{.id = 2};
    Order o3{.id = 3};
    Order o4{.id = 4};

    IntrusiveList<Order> list;
    list.push_back(&o1);
    list.push_back(&o2);
    list.push_back(&o3);
    list.push_back(&o4);
    REQUIRE(list.size() == 4);

    SECTION("Erase middle element") {
        list.erase(&o2);
        REQUIRE(list.size() == 3);
        REQUIRE(list.front() == &o1);
        REQUIRE(list.back() == &o4);

        // Verify linkage
        REQUIRE(o1.next == &o3);
        REQUIRE(o3.prev == &o1);
    }

    SECTION("Erase head element") {
        list.erase(&o1);
        REQUIRE(list.size() == 3);
        REQUIRE(list.front() == &o2);
        REQUIRE(o2.prev == nullptr);
    }

    SECTION("Erase tail element") {
        list.erase(&o4);
        REQUIRE(list.size() == 3);
        REQUIRE(list.back() == &o3);
        REQUIRE(o3.next == nullptr);
    }

    SECTION("Erase until empty") {
        list.erase(&o1);
        list.erase(&o2);
        list.erase(&o3);
        list.erase(&o4);
        REQUIRE(list.empty());
        REQUIRE(list.front() == nullptr);
        REQUIRE(list.back() == nullptr);
    }

    SECTION("Erase nullptr is safe") {
        list.erase(nullptr);
        REQUIRE(list.size() == 4);
    }
}

TEST_CASE("IntrusiveList - Iteration and Clear", "[intrusive_list]") {
    Order o1{.id = 10};
    Order o2{.id = 20};
    Order o3{.id = 30};

    IntrusiveList<Order> list;
    list.push_back(&o1);
    list.push_back(&o2);
    list.push_back(&o3);

    std::vector<OrderId> ids;
    for (const auto& order : list) {
        ids.push_back(order.id);
    }
    REQUIRE(ids == std::vector<OrderId>{10, 20, 30});

    list.clear();
    REQUIRE(list.empty());
    REQUIRE(list.size() == 0);
    REQUIRE(list.front() == nullptr);
    REQUIRE(list.back() == nullptr);
}

TEST_CASE("IntrusiveList - Move Semantics", "[intrusive_list]") {
    Order o1{.id = 100};
    Order o2{.id = 200};

    IntrusiveList<Order> list1;
    list1.push_back(&o1);
    list1.push_back(&o2);

    IntrusiveList<Order> list2(std::move(list1));
    REQUIRE(list1.empty());
    REQUIRE(list2.size() == 2);
    REQUIRE(list2.front() == &o1);
    REQUIRE(list2.back() == &o2);

    IntrusiveList<Order> list3;
    list3 = std::move(list2);
    REQUIRE(list2.empty());
    REQUIRE(list3.size() == 2);
    REQUIRE(list3.front() == &o1);
}

TEST_CASE("IntrusiveList - Idempotent Double-Erase and Unlinked Node Safety", "[intrusive_list]") {
    Order o1{.id = 1};
    Order o2{.id = 2};
    Order unlinked{.id = 99};

    IntrusiveList<Order> list;
    list.push_back(&o1);
    list.push_back(&o2);
    REQUIRE(list.size() == 2);

    // Erasing an unlinked node must be a safe no-op
    list.erase(&unlinked);
    REQUIRE(list.size() == 2);
    REQUIRE(list.front() == &o1);
    REQUIRE(list.back() == &o2);

    // Erasing o2 twice must not corrupt head_ or underflow size_
    list.erase(&o2);
    REQUIRE(list.size() == 1);
    list.erase(&o2);
    REQUIRE(list.size() == 1);
    REQUIRE(list.front() == &o1);
    REQUIRE(list.back() == &o1);
}
