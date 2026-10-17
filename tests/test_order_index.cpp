#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "orderbook/order_index.hpp"

using namespace orderbook;

TEST_CASE("FlatOrderIndex - Basic Operations", "[order_index]") {
    FlatOrderIndex index(16);
    REQUIRE(index.empty());
    REQUIRE(index.size() == 0);

    Order o1{.id = 1001};
    Order o2{.id = 1002};
    Order o3{.id = 1003};

    REQUIRE(index.insert(1001, &o1));
    REQUIRE(index.insert(1002, &o2));
    REQUIRE(index.insert(1003, &o3));
    REQUIRE(index.size() == 3);

    REQUIRE(index.find(1001) == &o1);
    REQUIRE(index.find(1002) == &o2);
    REQUIRE(index.find(1003) == &o3);
    REQUIRE(index.find(9999) == nullptr);

    REQUIRE(index.contains(1001));
    REQUIRE_FALSE(index.contains(9999));

    // Erase
    REQUIRE(index.erase(1002));
    REQUIRE(index.size() == 2);
    REQUIRE(index.find(1002) == nullptr);
    REQUIRE_FALSE(index.contains(1002));

    // Erase non-existent
    REQUIRE_FALSE(index.erase(9999));
}

TEST_CASE("FlatOrderIndex - Tombstone Reuse and Collisions", "[order_index]") {
    FlatOrderIndex index(8);

    Order o1{.id = 1};
    Order o2{.id = 2};

    index.insert(1, &o1);
    index.insert(2, &o2);

    // Erase creates tombstone
    REQUIRE(index.erase(1));
    REQUIRE(index.find(1) == nullptr);

    // Re-inserting reuse tombstone
    Order o1_new{.id = 1};
    REQUIRE(index.insert(1, &o1_new));
    REQUIRE(index.find(1) == &o1_new);
}

TEST_CASE("FlatOrderIndex - Large Volume Churn", "[order_index]") {
    constexpr size_t CAPACITY = 2000;
    FlatOrderIndex index(CAPACITY);

    std::vector<Order> orders(1000);
    for (size_t i = 0; i < 1000; ++i) {
        orders[i].id = i + 1;
        REQUIRE(index.insert(orders[i].id, &orders[i]));
    }
    REQUIRE(index.size() == 1000);

    for (size_t i = 0; i < 1000; ++i) {
        REQUIRE(index.find(orders[i].id) == &orders[i]);
    }

    for (size_t i = 0; i < 500; ++i) {
        REQUIRE(index.erase(orders[i].id));
    }
    REQUIRE(index.size() == 500);

    for (size_t i = 0; i < 500; ++i) {
        REQUIRE(index.find(orders[i].id) == nullptr);
    }
    for (size_t i = 500; i < 1000; ++i) {
        REQUIRE(index.find(orders[i].id) == &orders[i]);
    }
}
