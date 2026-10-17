#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>

namespace orderbook {

// 8-byte compact fixed-width Symbol Identifier fitting in a single 64-bit register
struct SymbolId {
    uint64_t raw_value{0};

    constexpr SymbolId() noexcept = default;
    constexpr explicit SymbolId(uint64_t raw) noexcept : raw_value(raw) {}

    static constexpr SymbolId from_string(std::string_view str) noexcept {
        uint64_t val = 0;
        size_t len = str.size() > 8 ? 8 : str.size();
        for (size_t i = 0; i < len; ++i) {
            val |= (static_cast<uint64_t>(static_cast<uint8_t>(str[i])) << (i * 8));
        }
        return SymbolId(val);
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return raw_value == 0; }

    constexpr auto operator<=>(const SymbolId&) const noexcept = default;
    constexpr bool operator==(const SymbolId&) const noexcept = default;

    [[nodiscard]] std::string to_string() const {
        std::string res;
        res.reserve(8);
        uint64_t temp = raw_value;
        for (size_t i = 0; i < 8; ++i) {
            char c = static_cast<char>(temp & 0xFF);
            if (c == '\0') {
                break;
            }
            res.push_back(c);
            temp >>= 8;
        }
        return res;
    }
};

static_assert(sizeof(SymbolId) == 8, "SymbolId must be exactly 8 bytes");
static_assert(std::is_trivially_copyable_v<SymbolId>, "SymbolId must be trivially copyable");
static_assert(std::is_standard_layout_v<SymbolId>, "SymbolId must have standard layout");

inline std::ostream& operator<<(std::ostream& os, const SymbolId& sym) {
    return os << sym.to_string();
}

// Core identifier and scalar aliases
using OrderId = uint64_t;
using ClientId = uint32_t;
using Volume = uint64_t;
using Quantity = Volume;
using Timestamp = uint64_t;
using PriceValue = int64_t;

inline constexpr OrderId INVALID_ORDER_ID = 0;
inline constexpr Volume ZERO_VOLUME = 0;
inline constexpr Timestamp ZERO_TIMESTAMP = 0;

// Order trading side
enum class Side : uint8_t { Buy = 0, Sell = 1 };

constexpr Side opposite_side(Side side) noexcept {
    return side == Side::Buy ? Side::Sell : Side::Buy;
}

constexpr std::string_view to_string(Side side) noexcept {
    switch (side) {
        case Side::Buy:
            return "BUY";
        case Side::Sell:
            return "SELL";
    }
    return "UNKNOWN";
}

inline std::ostream& operator<<(std::ostream& os, Side side) {
    return os << to_string(side);
}

// Supported order execution types
enum class OrderType : uint8_t { Limit = 0, Market = 1, IOC = 2, FOK = 3 };

constexpr std::string_view to_string(OrderType type) noexcept {
    switch (type) {
        case OrderType::Limit:
            return "LIMIT";
        case OrderType::Market:
            return "MARKET";
        case OrderType::IOC:
            return "IOC";
        case OrderType::FOK:
            return "FOK";
    }
    return "UNKNOWN";
}

inline std::ostream& operator<<(std::ostream& os, OrderType type) {
    return os << to_string(type);
}

// Order time-in-force instructions
enum class TimeInForce : uint8_t {
    GTC = 0,  // Good 'Til Cancelled
    IOC = 1,  // Immediate Or Cancel
    FOK = 2   // Fill Or Kill
};

constexpr std::string_view to_string(TimeInForce tif) noexcept {
    switch (tif) {
        case TimeInForce::GTC:
            return "GTC";
        case TimeInForce::IOC:
            return "IOC";
        case TimeInForce::FOK:
            return "FOK";
    }
    return "UNKNOWN";
}

inline std::ostream& operator<<(std::ostream& os, TimeInForce tif) {
    return os << to_string(tif);
}

// Order lifecycle states
enum class OrderStatus : uint8_t {
    New = 0,
    Accepted = 1,
    PartiallyFilled = 2,
    Filled = 3,
    Cancelled = 4,
    Rejected = 5
};

constexpr std::string_view to_string(OrderStatus status) noexcept {
    switch (status) {
        case OrderStatus::New:
            return "NEW";
        case OrderStatus::Accepted:
            return "ACCEPTED";
        case OrderStatus::PartiallyFilled:
            return "PARTIALLY_FILLED";
        case OrderStatus::Filled:
            return "FILLED";
        case OrderStatus::Cancelled:
            return "CANCELLED";
        case OrderStatus::Rejected:
            return "REJECTED";
    }
    return "UNKNOWN";
}

inline std::ostream& operator<<(std::ostream& os, OrderStatus status) {
    return os << to_string(status);
}

// Order rejection reason codes
enum class RejectReason : uint8_t {
    None = 0,
    InvalidPrice = 1,
    InvalidQuantity = 2,
    OrderNotFound = 3,
    InsufficientLiquidity = 4,
    DuplicateOrderId = 5,
    BookFull = 6
};

constexpr std::string_view to_string(RejectReason reason) noexcept {
    switch (reason) {
        case RejectReason::None:
            return "NONE";
        case RejectReason::InvalidPrice:
            return "INVALID_PRICE";
        case RejectReason::InvalidQuantity:
            return "INVALID_QUANTITY";
        case RejectReason::OrderNotFound:
            return "ORDER_NOT_FOUND";
        case RejectReason::InsufficientLiquidity:
            return "INSUFFICIENT_LIQUIDITY";
        case RejectReason::DuplicateOrderId:
            return "DUPLICATE_ORDER_ID";
        case RejectReason::BookFull:
            return "BOOK_FULL";
    }
    return "UNKNOWN";
}

inline std::ostream& operator<<(std::ostream& os, RejectReason reason) {
    return os << to_string(reason);
}

// C++20 Concepts
template<typename T>
concept NumericTick = std::integral<T> || std::floating_point<T>;

template<typename T>
concept SerializableEvent = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

}  // namespace orderbook

template<>
struct std::hash<orderbook::SymbolId> {
    size_t operator()(const orderbook::SymbolId& sym) const noexcept {
        return std::hash<uint64_t>{}(sym.raw_value);
    }
};
