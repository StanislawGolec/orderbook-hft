#include "orderbook/invariant_checker.hpp"

namespace orderbook {

bool InvariantChecker::check_no_crossed_book(const OrderBook& book) noexcept {
    if (book.has_bids() && book.has_asks()) {
        return book.best_bid() < book.best_ask();
    }
    return true;
}

bool InvariantChecker::check_volume_conservation(Volume total_submitted, Volume total_filled,
                                                 Volume total_resting, Volume total_cancelled,
                                                 Volume total_rejected) noexcept {
    return total_submitted == (total_filled + total_resting + total_cancelled + total_rejected);
}

InvariantReport InvariantChecker::check_book_consistency(const OrderBook& book) {
    if (!check_no_crossed_book(book)) {
        return InvariantReport{
            .passed = false,
            .error_message = "Invariant violation: Crossed book detected (Best Bid >= Best Ask)",
        };
    }

    size_t total_orders_counted = 0;

    // 1. Audit Bids
    const auto& bids = book.bids();
    Volume bids_computed_volume = 0;
    Price prev_bid_price = Price::max();

    for (size_t i = 0; i < bids.levels().size(); ++i) {
        const PriceLevel* level = bids.levels()[i];
        if (!level) {
            return InvariantReport{.passed = false,
                                   .error_message = "Null PriceLevel pointer found in bids"};
        }

        // Must be strictly descending
        if (i > 0 && !(level->price() < prev_bid_price)) {
            return InvariantReport{
                .passed = false,
                .error_message = "Bids price level ordering violated (not strictly descending)",
            };
        }
        prev_bid_price = level->price();

        // Audit orders in price level queue
        Volume level_orders_volume = 0;
        size_t level_orders_count = 0;

        for (const Order& order : level->orders()) {
            if (order.side != Side::Buy) {
                return InvariantReport{
                    .passed = false,
                    .error_message = "Order with non-Buy side located in bids price level",
                };
            }
            if (order.price != level->price()) {
                return InvariantReport{
                    .passed = false,
                    .error_message = "Order price does not match its containing price level",
                };
            }
            if (order.remaining_volume == 0) {
                return InvariantReport{
                    .passed = false,
                    .error_message =
                        "Order with zero remaining volume found resting in price level",
                };
            }

            // Verify order exists in fast lookup table
            const Order* indexed_order = book.find_order(order.id);
            if (indexed_order != &order) {
                return InvariantReport{
                    .passed = false,
                    .error_message = "Order in price level is missing or mismatched in order_index",
                };
            }

            level_orders_volume += order.remaining_volume;
            ++level_orders_count;
        }

        if (level->total_volume() != level_orders_volume) {
            return InvariantReport{
                .passed = false,
                .error_message = "PriceLevel total_volume does not match sum of order volumes",
            };
        }

        if (level->order_count() != level_orders_count) {
            return InvariantReport{
                .passed = false,
                .error_message = "PriceLevel order_count does not match count of linked orders",
            };
        }

        bids_computed_volume += level->total_volume();
        total_orders_counted += level_orders_count;
    }

    if (bids.total_volume() != bids_computed_volume) {
        return InvariantReport{
            .passed = false,
            .error_message = "Bids total_volume does not match aggregate of level volumes",
        };
    }

    // 2. Audit Asks
    const auto& asks = book.asks();
    Volume asks_computed_volume = 0;
    Price prev_ask_price = Price::zero();

    for (size_t i = 0; i < asks.levels().size(); ++i) {
        const PriceLevel* level = asks.levels()[i];
        if (!level) {
            return InvariantReport{.passed = false,
                                   .error_message = "Null PriceLevel pointer found in asks"};
        }

        // Must be strictly ascending
        if (i > 0 && !(level->price() > prev_ask_price)) {
            return InvariantReport{
                .passed = false,
                .error_message = "Asks price level ordering violated (not strictly ascending)",
            };
        }
        prev_ask_price = level->price();

        Volume level_orders_volume = 0;
        size_t level_orders_count = 0;

        for (const Order& order : level->orders()) {
            if (order.side != Side::Sell) {
                return InvariantReport{
                    .passed = false,
                    .error_message = "Order with non-Sell side located in asks price level",
                };
            }
            if (order.price != level->price()) {
                return InvariantReport{
                    .passed = false,
                    .error_message = "Order price does not match its containing price level",
                };
            }
            if (order.remaining_volume == 0) {
                return InvariantReport{
                    .passed = false,
                    .error_message =
                        "Order with zero remaining volume found resting in price level",
                };
            }

            const Order* indexed_order = book.find_order(order.id);
            if (indexed_order != &order) {
                return InvariantReport{
                    .passed = false,
                    .error_message = "Order in price level is missing or mismatched in order_index",
                };
            }

            level_orders_volume += order.remaining_volume;
            ++level_orders_count;
        }

        if (level->total_volume() != level_orders_volume) {
            return InvariantReport{
                .passed = false,
                .error_message = "PriceLevel total_volume does not match sum of order volumes",
            };
        }

        if (level->order_count() != level_orders_count) {
            return InvariantReport{
                .passed = false,
                .error_message = "PriceLevel order_count does not match count of linked orders",
            };
        }

        asks_computed_volume += level->total_volume();
        total_orders_counted += level_orders_count;
    }

    if (asks.total_volume() != asks_computed_volume) {
        return InvariantReport{
            .passed = false,
            .error_message = "Asks total_volume does not match aggregate of level volumes",
        };
    }

    // 3. Global Order Count Audit
    if (book.order_count() != total_orders_counted) {
        return InvariantReport{
            .passed = false,
            .error_message = "OrderBook order_count does not match total orders counted in levels",
        };
    }

    if (book.order_index().size() != total_orders_counted) {
        return InvariantReport{
            .passed = false,
            .error_message = "FlatOrderIndex size does not match total orders counted in levels",
        };
    }

    return InvariantReport{.passed = true, .error_message = {}};
}

}  // namespace orderbook
