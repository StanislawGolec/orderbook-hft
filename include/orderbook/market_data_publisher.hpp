#pragma once

#include <cstdint>

#include "orderbook/market_data_protocol.hpp"
#include "orderbook/matching_engine.hpp"
#include "orderbook/order_book.hpp"

namespace orderbook {

// Interface for receiving serialized binary feed messages
class IBinaryFeedSink {
public:
    virtual ~IBinaryFeedSink() = default;
    virtual void on_add_order(const AddOrderMsg& msg) noexcept { (void)msg; }
    virtual void on_order_executed(const OrderExecutedMsg& msg) noexcept { (void)msg; }
    virtual void on_order_cancelled(const OrderCancelMsg& msg) noexcept { (void)msg; }
    virtual void on_order_deleted(const OrderDeleteMsg& msg) noexcept { (void)msg; }
    virtual void on_order_replaced(const OrderReplaceMsg& msg) noexcept { (void)msg; }
    virtual void on_depth_snapshot(const DepthSnapshotMsg& msg) noexcept { (void)msg; }
};

// Market Data Publisher converting matching engine events into binary L2/L3 market data streams
class MarketDataPublisher : public IMatchingListener {
public:
    explicit MarketDataPublisher(IBinaryFeedSink* sink = nullptr);

    void set_sink(IBinaryFeedSink* sink) noexcept { sink_ = sink; }
    [[nodiscard]] IBinaryFeedSink* sink() const noexcept { return sink_; }

    [[nodiscard]] uint64_t current_sequence_number() const noexcept { return sequence_number_; }
    void reset_sequence_number(uint64_t start_seq = 0) noexcept { sequence_number_ = start_seq; }

    // Generates an L2 Market Depth snapshot of the current book
    DepthSnapshotMsg create_depth_snapshot(const OrderBook& book, Timestamp ts = 0) noexcept;

    // IMatchingListener implementation
    void on_order_rested(const OrderRested& event) noexcept override;
    void on_order_cancelled(const OrderCancelled& event) noexcept override;
    void on_order_filled(const OrderFilled& event) noexcept override;
    void on_order_amended(const OrderAmended& event) noexcept override;
    void on_trade(const TradeEvent& event) noexcept override;

private:
    IBinaryFeedSink* sink_{nullptr};
    uint64_t sequence_number_{0};
};

}  // namespace orderbook
