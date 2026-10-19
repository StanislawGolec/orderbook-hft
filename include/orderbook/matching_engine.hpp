#pragma once

#include <cstdint>

#include "orderbook/events.hpp"
#include "orderbook/order.hpp"
#include "orderbook/order_book.hpp"
#include "orderbook/price.hpp"
#include "orderbook/types.hpp"

namespace orderbook {

// Representation of an incoming order request for the matching engine
struct OrderRequest {
    OrderId id{INVALID_ORDER_ID};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    Price price{Price::zero()};
    Volume volume{0};
    Timestamp timestamp{0};
    TimeInForce tif{TimeInForce::GTC};
};

// Interface for receiving zero-allocation execution event callbacks
class IMatchingListener {
public:
    virtual ~IMatchingListener() = default;

    virtual void on_order_accepted(const OrderAccepted& event) noexcept { (void)event; }
    virtual void on_order_rested(const OrderRested& event) noexcept { (void)event; }
    virtual void on_order_rejected(const OrderRejected& event) noexcept { (void)event; }
    virtual void on_order_cancelled(const OrderCancelled& event) noexcept { (void)event; }
    virtual void on_order_filled(const OrderFilled& event) noexcept { (void)event; }
    virtual void on_order_amended(const OrderAmended& event) noexcept { (void)event; }
    virtual void on_trade(const TradeEvent& event) noexcept { (void)event; }
};

// High-Performance Price-Time Priority Matching Engine
class MatchingEngine {
public:
    explicit MatchingEngine(OrderBook& book, IMatchingListener* listener = nullptr);

    void set_listener(IMatchingListener* listener) noexcept { listener_ = listener; }
    [[nodiscard]] IMatchingListener* listener() const noexcept { return listener_; }

    [[nodiscard]] OrderBook& book() noexcept { return book_; }
    [[nodiscard]] const OrderBook& book() const noexcept { return book_; }

    // Core matching event handler
    bool process_order(const OrderRequest& request);

    // Convenience overload for submitting orders
    bool submit_order(OrderId id, Side side, Price price, Volume volume, Timestamp ts,
                      OrderType type = OrderType::Limit, TimeInForce tif = TimeInForce::GTC) {
        return process_order(OrderRequest{.id = id,
                                          .side = side,
                                          .type = type,
                                          .price = price,
                                          .volume = volume,
                                          .timestamp = ts,
                                          .tif = tif});
    }

    // In-place order amendment handler (retains priority on volume decrease)
    bool process_amend(const AmendOrderRequest& request);

    // Convenience overload for amending orders
    bool amend_order(OrderId id, Price new_price, Volume new_volume, Timestamp ts = 0) {
        return process_amend(AmendOrderRequest{
            .id = id, .new_price = new_price, .new_volume = new_volume, .timestamp = ts});
    }

    // Cancels a resting order in O(1)
    bool cancel_order(OrderId id, Timestamp ts = 0);

    [[nodiscard]] uint64_t last_match_id() const noexcept { return match_id_sequence_; }
    void reset_match_id(uint64_t start_id = 0) noexcept { match_id_sequence_ = start_id; }

private:
    bool execute_matching_and_rest(const OrderRequest& request, bool emit_rested_event);

    OrderBook& book_;
    IMatchingListener* listener_{nullptr};
    uint64_t match_id_sequence_{0};
};

}  // namespace orderbook
