#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "orderbook/price.hpp"

using namespace orderbook;

TEST_CASE("Price - Compile-time Constants and Static Assertions", "[price]") {
    STATIC_REQUIRE(Price::DECIMALS == 4);
    STATIC_REQUIRE(Price::SCALE == 10000);
    STATIC_REQUIRE(Price::zero().raw() == 0);
    STATIC_REQUIRE(Price::from_raw(10000).raw() == 10000);
    STATIC_REQUIRE(Price::from_parts(50, 2500).raw() == 502500);

    constexpr Price p1 = Price::from_raw(10000);
    constexpr Price p2 = Price::from_raw(20000);
    STATIC_REQUIRE((p1 + p2).raw() == 30000);
    STATIC_REQUIRE((p2 - p1).raw() == 10000);
    STATIC_REQUIRE((p1 * 3).raw() == 30000);
    STATIC_REQUIRE((p2 / 2).raw() == 10000);
    STATIC_REQUIRE(p1 < p2);
    STATIC_REQUIRE(p2 > p1);
    STATIC_REQUIRE(p1 != p2);
}

TEST_CASE("Price - Factory Methods and Conversions", "[price]") {
    SECTION("Default Construction") {
        Price p;
        REQUIRE(p.raw() == 0);
        REQUIRE(p.is_zero());
        REQUIRE_FALSE(p.is_positive());
        REQUIRE_FALSE(p.is_negative());
    }

    SECTION("from_double with Rounding") {
        Price p1 = Price::from_double(10.5);
        REQUIRE(p1.raw() == 105000);
        REQUIRE(p1.to_double() == 10.5);

        Price p2 = Price::from_double(0.0001);
        REQUIRE(p2.raw() == 1);

        // Rounding half up
        Price p3 = Price::from_double(1.00006);
        REQUIRE(p3.raw() == 10001);

        Price p4 = Price::from_double(1.00004);
        REQUIRE(p4.raw() == 10000);

        // Negative values
        Price p5 = Price::from_double(-10.25);
        REQUIRE(p5.raw() == -102500);
        REQUIRE(p5.is_negative());
    }

    SECTION("from_parts") {
        Price p1 = Price::from_parts(150, 7500);
        REQUIRE(p1.raw() == 1507500);
        REQUIRE(p1.whole_part() == 150);
        REQUIRE(p1.fractional_part() == 7500);

        Price p2 = Price::from_parts(-20, 5000);
        REQUIRE(p2.raw() == -205000);
        REQUIRE(p2.whole_part() == -20);
        REQUIRE(p2.fractional_part() == 5000);
    }
}

TEST_CASE("Price - Arithmetic Operations", "[price]") {
    Price a = Price::from_double(100.25);
    Price b = Price::from_double(50.75);

    SECTION("Addition") {
        Price c = a + b;
        REQUIRE(c == Price::from_double(151.00));

        Price d = a;
        d += b;
        REQUIRE(d == c);
    }

    SECTION("Subtraction") {
        Price c = a - b;
        REQUIRE(c == Price::from_double(49.50));

        Price d = a;
        d -= b;
        REQUIRE(d == c);
    }

    SECTION("Scalar Multiplication and Division") {
        Price c = a * 2;
        REQUIRE(c == Price::from_double(200.50));

        Price d = c / 2;
        REQUIRE(d == a);
    }

    SECTION("Unary Negation") {
        Price c = -a;
        REQUIRE(c.raw() == -a.raw());
        REQUIRE(c.is_negative());
    }
}

TEST_CASE("Price - Comparisons", "[price]") {
    Price low = Price::from_double(99.50);
    Price high = Price::from_double(100.50);
    Price equal = Price::from_double(99.50);

    REQUIRE(low < high);
    REQUIRE(low <= high);
    REQUIRE(low <= equal);
    REQUIRE(high > low);
    REQUIRE(high >= low);
    REQUIRE(low == equal);
    REQUIRE(low != high);
    REQUIRE((low <=> high) == std::strong_ordering::less);
    REQUIRE((high <=> low) == std::strong_ordering::greater);
    REQUIRE((low <=> equal) == std::strong_ordering::equal);
}

TEST_CASE("Price - String Formatting and Streaming", "[price]") {
    Price p1 = Price::from_double(100.50);
    REQUIRE(p1.to_string() == "100.5000");

    Price p2 = Price::from_double(0.0005);
    REQUIRE(p2.to_string() == "0.0005");

    Price p3 = Price::from_double(0.0);
    REQUIRE(p3.to_string() == "0.0000");

    Price p4 = Price::from_double(-0.25);
    REQUIRE(p4.to_string() == "-0.2500");

    Price p5 = Price::from_double(-42.1234);
    REQUIRE(p5.to_string() == "-42.1234");

    std::ostringstream oss;
    oss << p1;
    REQUIRE(oss.str() == "100.5000");
}

TEST_CASE("Price - Standard Hash and Container Usage", "[price]") {
    std::unordered_set<Price> price_set;
    price_set.insert(Price::from_double(10.0));
    price_set.insert(Price::from_double(20.0));
    price_set.insert(Price::from_double(10.0));

    REQUIRE(price_set.size() == 2);
    REQUIRE(price_set.contains(Price::from_double(10.0)));
    REQUIRE(price_set.contains(Price::from_double(20.0)));
    REQUIRE_FALSE(price_set.contains(Price::from_double(30.0)));

    std::unordered_map<Price, Volume> volume_by_price;
    volume_by_price[Price::from_double(100.0)] = 500;
    volume_by_price[Price::from_double(101.0)] = 1200;

    REQUIRE(volume_by_price[Price::from_double(100.0)] == 500);
    REQUIRE(volume_by_price[Price::from_double(101.0)] == 1200);
}
