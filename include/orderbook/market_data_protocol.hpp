#pragma once

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#if defined(_MSC_VER)
#include <stdlib.h>
#endif

#include "orderbook/price.hpp"
#include "orderbook/types.hpp"

namespace orderbook {

// Endianness byte-swap utilities for cross-platform network protocol encoding
template<typename T>
constexpr T to_network_endian(T value) noexcept {
    if constexpr (std::endian::native == std::endian::big) {
        return value;
    } else {
        if constexpr (sizeof(T) == 1) {
            return value;
        } else if constexpr (sizeof(T) == 2) {
#if defined(_MSC_VER)
            return static_cast<T>(_byteswap_ushort(static_cast<uint16_t>(value)));
#else
            return static_cast<T>(__builtin_bswap16(static_cast<uint16_t>(value)));
#endif
        } else if constexpr (sizeof(T) == 4) {
#if defined(_MSC_VER)
            return static_cast<T>(_byteswap_ulong(static_cast<uint32_t>(value)));
#else
            return static_cast<T>(__builtin_bswap32(static_cast<uint32_t>(value)));
#endif
        } else if constexpr (sizeof(T) == 8) {
#if defined(_MSC_VER)
            return static_cast<T>(_byteswap_uint64(static_cast<uint64_t>(value)));
#else
            return static_cast<T>(__builtin_bswap64(static_cast<uint64_t>(value)));
#endif
        }
    }
}

template<typename T>
constexpr T from_network_endian(T value) noexcept {
    return to_network_endian(value);
}

// ITCH/OUCH Style Binary Gateway Protocol Message Types
enum class MarketDataMsgType : uint8_t {
    AddOrder = 'A',
    OrderExecuted = 'E',
    OrderCancel = 'X',
    OrderDelete = 'D',
    DepthSnapshot = 'S'
};

#pragma pack(push, 1)

// L3 Incremental: New resting order added to the book
struct AddOrderMsg {
    MarketDataMsgType msg_type{MarketDataMsgType::AddOrder};
    uint64_t sequence_number{0};
    OrderId order_id{0};
    Side side{Side::Buy};
    int64_t price_raw{0};
    Volume volume{0};
    Timestamp timestamp{0};
};

// L3 Incremental: Order executed against crossing taker
struct OrderExecutedMsg {
    MarketDataMsgType msg_type{MarketDataMsgType::OrderExecuted};
    uint64_t sequence_number{0};
    OrderId order_id{0};
    uint64_t match_id{0};
    int64_t price_raw{0};
    Volume executed_volume{0};
    Timestamp timestamp{0};
};

// L3 Incremental: Order volume partially cancelled
struct OrderCancelMsg {
    MarketDataMsgType msg_type{MarketDataMsgType::OrderCancel};
    uint64_t sequence_number{0};
    OrderId order_id{0};
    Volume cancelled_volume{0};
    Timestamp timestamp{0};
};

// L3 Incremental: Order completely removed from the book
struct OrderDeleteMsg {
    MarketDataMsgType msg_type{MarketDataMsgType::OrderDelete};
    uint64_t sequence_number{0};
    OrderId order_id{0};
    Timestamp timestamp{0};
};

// L2 Depth level price, volume, and order aggregation
struct LevelDataMsg {
    int64_t price_raw{0};
    Volume volume{0};
    uint32_t order_count{0};
};

inline constexpr size_t MAX_SNAPSHOT_LEVELS = 10;

// L2 Market Depth snapshot of top N price levels
struct DepthSnapshotMsg {
    MarketDataMsgType msg_type{MarketDataMsgType::DepthSnapshot};
    uint64_t sequence_number{0};
    uint32_t bid_level_count{0};
    uint32_t ask_level_count{0};
    Timestamp timestamp{0};
    LevelDataMsg bids[MAX_SNAPSHOT_LEVELS]{};
    LevelDataMsg asks[MAX_SNAPSHOT_LEVELS]{};
};

#pragma pack(pop)

// Compile-time checks enforcing zero serialization overhead
static_assert(std::is_trivially_copyable_v<AddOrderMsg>, "AddOrderMsg must be trivially copyable");
static_assert(std::is_standard_layout_v<AddOrderMsg>, "AddOrderMsg must have standard layout");

static_assert(std::is_trivially_copyable_v<OrderExecutedMsg>,
              "OrderExecutedMsg must be trivially copyable");
static_assert(std::is_standard_layout_v<OrderExecutedMsg>,
              "OrderExecutedMsg must have standard layout");

static_assert(std::is_trivially_copyable_v<OrderCancelMsg>,
              "OrderCancelMsg must be trivially copyable");
static_assert(std::is_standard_layout_v<OrderCancelMsg>,
              "OrderCancelMsg must have standard layout");

static_assert(std::is_trivially_copyable_v<OrderDeleteMsg>,
              "OrderDeleteMsg must be trivially copyable");
static_assert(std::is_standard_layout_v<OrderDeleteMsg>,
              "OrderDeleteMsg must have standard layout");

static_assert(std::is_trivially_copyable_v<DepthSnapshotMsg>,
              "DepthSnapshotMsg must be trivially copyable");
static_assert(std::is_standard_layout_v<DepthSnapshotMsg>,
              "DepthSnapshotMsg must have standard layout");

}  // namespace orderbook
