#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "orderbook/market_data_protocol.hpp"
#include "orderbook/price.hpp"
#include "orderbook/types.hpp"

namespace orderbook {

struct SequenceGap {
    uint64_t expected_seq{0};
    uint64_t received_seq{0};
    uint64_t missing_count{0};
};

struct BBOQuote {
    Price bid_price{Price::zero()};
    Volume bid_volume{0};
    Price ask_price{Price::zero()};
    Volume ask_volume{0};
};

// Client-side Market Data Feed Handler with sequence gap detection and snapshot recovery
class FeedHandler {
public:
    FeedHandler() = default;

    // Ingests raw binary packet from network
    bool process_message(const uint8_t* buffer, size_t length);

    // Specific message handlers
    bool on_add_order(const AddOrderMsg& msg);
    bool on_order_executed(const OrderExecutedMsg& msg);
    bool on_order_cancelled(const OrderCancelMsg& msg);
    bool on_order_deleted(const OrderDeleteMsg& msg);
    bool on_depth_snapshot(const DepthSnapshotMsg& msg);

    // Resynchronization via snapshot
    void apply_snapshot(const DepthSnapshotMsg& snapshot);

    [[nodiscard]] uint64_t expected_sequence_number() const noexcept {
        return expected_sequence_number_;
    }
    [[nodiscard]] uint64_t messages_processed() const noexcept { return messages_processed_; }
    [[nodiscard]] uint64_t gap_count() const noexcept { return gap_count_; }
    [[nodiscard]] uint64_t duplicate_count() const noexcept { return duplicate_count_; }
    [[nodiscard]] bool has_detected_gaps() const noexcept { return gap_count_ > 0; }
    [[nodiscard]] bool is_in_sync() const noexcept { return in_sync_; }

    [[nodiscard]] const std::vector<SequenceGap>& detected_gaps() const noexcept {
        return detected_gaps_;
    }

    [[nodiscard]] BBOQuote current_bbo() const noexcept { return bbo_; }

    void reset() noexcept;

private:
    bool check_sequence(uint64_t seq);

    uint64_t expected_sequence_number_{0};
    uint64_t messages_processed_{0};
    uint64_t gap_count_{0};
    uint64_t duplicate_count_{0};
    bool in_sync_{true};
    std::vector<SequenceGap> detected_gaps_{};
    BBOQuote bbo_{};
};

}  // namespace orderbook
