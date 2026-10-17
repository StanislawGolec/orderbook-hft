#include "orderbook/matching_engine.hpp"

#include <algorithm>

namespace orderbook {

MatchingEngine::MatchingEngine(OrderBook& book, IMatchingListener* listener)
    : book_(book), listener_(listener) {}

bool MatchingEngine::process_order(const OrderRequest& request) {
    if (request.id == INVALID_ORDER_ID) [[unlikely]] {
        if (listener_) {
            listener_->on_order_rejected(OrderRejected{
                .order_id = request.id,
                .reason = RejectReason::InvalidQuantity,
                .timestamp = request.timestamp,
            });
        }
        return false;
    }

    if (request.volume == 0) [[unlikely]] {
        if (listener_) {
            listener_->on_order_rejected(OrderRejected{
                .order_id = request.id,
                .reason = RejectReason::InvalidQuantity,
                .timestamp = request.timestamp,
            });
        }
        return false;
    }

    if (book_.find_order(request.id) != nullptr) [[unlikely]] {
        if (listener_) {
            listener_->on_order_rejected(OrderRejected{
                .order_id = request.id,
                .reason = RejectReason::DuplicateOrderId,
                .timestamp = request.timestamp,
            });
        }
        return false;
    }

    const bool is_market = (request.type == OrderType::Market);
    const bool is_fok = (request.type == OrderType::FOK || request.tif == TimeInForce::FOK);
    const bool is_ioc = (request.type == OrderType::IOC || request.tif == TimeInForce::IOC);

    if (!is_market && request.price <= Price::zero()) [[unlikely]] {
        if (listener_) {
            listener_->on_order_rejected(OrderRejected{
                .order_id = request.id,
                .reason = RejectReason::InvalidPrice,
                .timestamp = request.timestamp,
            });
        }
        return false;
    }

    if (is_market) {
        bool opposing_empty =
            (request.side == Side::Buy) ? book_.asks().empty() : book_.bids().empty();
        if (opposing_empty) [[unlikely]] {
            if (listener_) {
                listener_->on_order_rejected(OrderRejected{
                    .order_id = request.id,
                    .reason = RejectReason::InsufficientLiquidity,
                    .timestamp = request.timestamp,
                });
            }
            return false;
        }
    }

    if (is_fok) {
        Price limit_price =
            is_market ? (request.side == Side::Buy ? Price::max() : Price::zero()) : request.price;
        Volume avail = (request.side == Side::Buy)
                           ? book_.asks().available_volume_up_to(limit_price)
                           : book_.bids().available_volume_up_to(limit_price);
        if (avail < request.volume) [[unlikely]] {
            if (listener_) {
                listener_->on_order_rejected(OrderRejected{
                    .order_id = request.id,
                    .reason = RejectReason::InsufficientLiquidity,
                    .timestamp = request.timestamp,
                });
            }
            return false;
        }
    }

    if (listener_) {
        listener_->on_order_accepted(OrderAccepted{
            .order_id = request.id,
            .price = is_market ? Price::zero() : request.price,
            .quantity = request.volume,
            .side = request.side,
            .type = request.type,
            .timestamp = request.timestamp,
        });
    }

    Volume remaining_qty = request.volume;

    if (request.side == Side::Buy) {
        while (remaining_qty > 0 && !book_.asks().empty()) {
            PriceLevel* best_level = book_.asks().best_level();
            if (!is_market && best_level->price() > request.price) {
                break;
            }

            Price match_price = best_level->price();
            Order* maker = best_level->orders().front();
            Volume match_qty = std::min(remaining_qty, maker->remaining_volume);
            uint64_t match_id = ++match_id_sequence_;
            remaining_qty -= match_qty;

            if (listener_) {
                listener_->on_trade(TradeEvent{
                    .match_id = match_id,
                    .maker_order_id = maker->id,
                    .taker_order_id = request.id,
                    .price = match_price,
                    .quantity = match_qty,
                    .taker_side = Side::Buy,
                    .timestamp = request.timestamp,
                });
                listener_->on_order_filled(OrderFilled{
                    .order_id = maker->id,
                    .price = match_price,
                    .filled_quantity = match_qty,
                    .remaining_quantity = maker->remaining_volume - match_qty,
                    .timestamp = request.timestamp,
                });
                listener_->on_order_filled(OrderFilled{
                    .order_id = request.id,
                    .price = match_price,
                    .filled_quantity = match_qty,
                    .remaining_quantity = remaining_qty,
                    .timestamp = request.timestamp,
                });
            }

            if (match_qty == maker->remaining_volume) [[likely]] {
                book_.remove_filled_order(maker);
            } else [[unlikely]] {
                book_.reduce_order_volume(maker, match_qty);
            }
        }
    } else {
        while (remaining_qty > 0 && !book_.bids().empty()) {
            PriceLevel* best_level = book_.bids().best_level();
            if (!is_market && best_level->price() < request.price) {
                break;
            }

            Price match_price = best_level->price();
            Order* maker = best_level->orders().front();
            Volume match_qty = std::min(remaining_qty, maker->remaining_volume);
            uint64_t match_id = ++match_id_sequence_;
            remaining_qty -= match_qty;

            if (listener_) {
                listener_->on_trade(TradeEvent{
                    .match_id = match_id,
                    .maker_order_id = maker->id,
                    .taker_order_id = request.id,
                    .price = match_price,
                    .quantity = match_qty,
                    .taker_side = Side::Sell,
                    .timestamp = request.timestamp,
                });
                listener_->on_order_filled(OrderFilled{
                    .order_id = maker->id,
                    .price = match_price,
                    .filled_quantity = match_qty,
                    .remaining_quantity = maker->remaining_volume - match_qty,
                    .timestamp = request.timestamp,
                });
                listener_->on_order_filled(OrderFilled{
                    .order_id = request.id,
                    .price = match_price,
                    .filled_quantity = match_qty,
                    .remaining_quantity = remaining_qty,
                    .timestamp = request.timestamp,
                });
            }

            if (match_qty == maker->remaining_volume) [[likely]] {
                book_.remove_filled_order(maker);
            } else [[unlikely]] {
                book_.reduce_order_volume(maker, match_qty);
            }
        }
    }

    if (remaining_qty > 0) {
        if (is_market || is_ioc || is_fok) {
            if (listener_) {
                listener_->on_order_cancelled(OrderCancelled{
                    .order_id = request.id,
                    .price = is_market ? Price::zero() : request.price,
                    .remaining_quantity = remaining_qty,
                    .side = request.side,
                    .timestamp = request.timestamp,
                });
            }
        } else {
            Order* resting =
                book_.add_limit_order(request.id, request.side, request.price, remaining_qty,
                                      request.timestamp, request.tif, request.volume);
            if (!resting) [[unlikely]] {
                if (listener_) {
                    listener_->on_order_rejected(OrderRejected{
                        .order_id = request.id,
                        .reason = RejectReason::BookFull,
                        .timestamp = request.timestamp,
                    });
                }
                return false;
            }
        }
    }

    return true;
}

bool MatchingEngine::cancel_order(OrderId id, Timestamp ts) {
    Order* order = book_.find_order(id);
    if (!order) [[unlikely]] {
        if (listener_) {
            listener_->on_order_rejected(OrderRejected{
                .order_id = id,
                .reason = RejectReason::OrderNotFound,
                .timestamp = ts,
            });
        }
        return false;
    }

    OrderCancelled event{
        .order_id = id,
        .price = order->price,
        .remaining_quantity = order->remaining_volume,
        .side = order->side,
        .timestamp = ts,
    };

    bool removed = book_.cancel_order(id);
    if (removed && listener_) {
        listener_->on_order_cancelled(event);
    }
    return removed;
}

}  // namespace orderbook
