#pragma once

#include <cmath>
#include <compare>
#include <cstdint>
#include <functional>
#include <limits>
#include <ostream>
#include <string>

#include "orderbook/types.hpp"

namespace orderbook {

// Fixed-point price with 4 decimal places (scaling factor 10,000)
// Eliminates floating-point non-determinism, rounding errors, and CPU float penalty
class Price {
public:
    static constexpr uint32_t DECIMALS = 4;
    static constexpr int64_t SCALE = 10000;

    constexpr Price() noexcept = default;

    explicit constexpr Price(int64_t raw_value) noexcept : raw_value_(raw_value) {}

    static constexpr Price from_raw(int64_t raw) noexcept { return Price(raw); }

    static constexpr Price from_double(double value) noexcept {
        if (value >= 0.0) {
            return Price(static_cast<int64_t>(value * static_cast<double>(SCALE) + 0.5));
        }
        return Price(static_cast<int64_t>(value * static_cast<double>(SCALE) - 0.5));
    }

    static constexpr Price from_parts(int64_t whole, int64_t fractional) noexcept {
        if (whole >= 0) {
            return Price(whole * SCALE + (fractional % SCALE));
        }
        return Price(whole * SCALE - (fractional % SCALE));
    }

    static constexpr Price min() noexcept { return Price(std::numeric_limits<int64_t>::min()); }

    static constexpr Price max() noexcept { return Price(std::numeric_limits<int64_t>::max()); }

    static constexpr Price zero() noexcept { return Price(0); }

    constexpr int64_t raw() const noexcept { return raw_value_; }

    constexpr double to_double() const noexcept {
        return static_cast<double>(raw_value_) / static_cast<double>(SCALE);
    }

    constexpr int64_t whole_part() const noexcept { return raw_value_ / SCALE; }

    constexpr int64_t fractional_part() const noexcept {
        int64_t frac = raw_value_ % SCALE;
        return frac < 0 ? -frac : frac;
    }

    constexpr bool is_zero() const noexcept { return raw_value_ == 0; }

    constexpr bool is_positive() const noexcept { return raw_value_ > 0; }

    constexpr bool is_negative() const noexcept { return raw_value_ < 0; }

    // Arithmetic operators
    constexpr Price operator+(Price rhs) const noexcept {
        return Price(raw_value_ + rhs.raw_value_);
    }

    constexpr Price operator-(Price rhs) const noexcept {
        return Price(raw_value_ - rhs.raw_value_);
    }

    constexpr Price operator*(int64_t multiplier) const noexcept {
        return Price(raw_value_ * multiplier);
    }

    constexpr Price operator/(int64_t divisor) const noexcept {
        return Price(raw_value_ / divisor);
    }

    constexpr Price& operator+=(Price rhs) noexcept {
        raw_value_ += rhs.raw_value_;
        return *this;
    }

    constexpr Price& operator-=(Price rhs) noexcept {
        raw_value_ -= rhs.raw_value_;
        return *this;
    }

    constexpr Price operator-() const noexcept { return Price(-raw_value_); }

    // Three-way comparison (C++20)
    constexpr auto operator<=>(const Price&) const noexcept = default;
    constexpr bool operator==(const Price&) const noexcept = default;

    // Formatting helper
    std::string to_string() const {
        std::string result;
        int64_t whole = raw_value_ / SCALE;
        int64_t frac = raw_value_ % SCALE;
        if (frac < 0) {
            frac = -frac;
        }

        if (raw_value_ < 0 && whole == 0) {
            result += "-";
        }
        result += std::to_string(whole);
        result += ".";

        std::string frac_str = std::to_string(frac);
        if (frac_str.length() < DECIMALS) {
            result.append(DECIMALS - frac_str.length(), '0');
        }
        result += frac_str;
        return result;
    }

    friend std::ostream& operator<<(std::ostream& os, Price price) {
        return os << price.to_string();
    }

private:
    int64_t raw_value_{0};
};

// Static verification of mechanical layout
static_assert(sizeof(Price) == 8, "Price must be exactly 8 bytes");
static_assert(std::is_trivially_copyable_v<Price>, "Price must be trivially copyable");
static_assert(std::is_standard_layout_v<Price>, "Price must have standard layout");

}  // namespace orderbook

// Standard hash specialization
template<>
struct std::hash<orderbook::Price> {
    size_t operator()(orderbook::Price p) const noexcept { return std::hash<int64_t>{}(p.raw()); }
};
