#pragma once

#include <string>

#include "orderbook/order_book.hpp"
#include "orderbook/types.hpp"

namespace orderbook {

struct InvariantReport {
    bool passed{true};
    std::string error_message{};
};

// Comprehensive QA validator auditing financial invariants and internal data structures
class InvariantChecker {
public:
    // Invariant 1: No Crossed Book
    // At no point may Best Bid >= Best Ask after matching completes
    [[nodiscard]] static bool check_no_crossed_book(const OrderBook& book) noexcept;

    // Invariant 2: Conservation of Shares and Volume
    // Total Submitted == Total Filled + Total Resting + Total Cancelled + Total Rejected
    [[nodiscard]] static bool check_volume_conservation(Volume total_submitted, Volume total_filled,
                                                        Volume total_resting,
                                                        Volume total_cancelled,
                                                        Volume total_rejected = 0) noexcept;

    // Invariant 3: Structural consistency and FIFO ordering
    // Verifies all price levels, intrusive lists, order counts, and hash table mappings
    [[nodiscard]] static InvariantReport check_book_consistency(const OrderBook& book);
};

}  // namespace orderbook
