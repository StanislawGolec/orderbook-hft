#include "orderbook/market_data_publisher.hpp"

#include <algorithm>

namespace orderbook {

MarketDataPublisher::MarketDataPublisher(IBinaryFeedSink* sink) : sink_(sink) {}

DepthSnapshotMsg MarketDataPublisher::create_depth_snapshot(const OrderBook& book,
                                                            Timestamp ts) noexcept {
    DepthSnapshotMsg snapshot{
        .msg_type = MarketDataMsgType::DepthSnapshot,
        .sequence_number = ++sequence_number_,
        .bid_level_count = 0,
        .ask_level_count = 0,
        .timestamp = ts,
    };

    // Bids depth
    const auto& bids = book.bids();
    size_t bid_count = std::min(bids.levels().size(), MAX_SNAPSHOT_LEVELS);
    snapshot.bid_level_count = static_cast<uint32_t>(bid_count);
    for (size_t i = 0; i < bid_count; ++i) {
        const PriceLevel* level = bids.levels()[i];
        snapshot.bids[i] = LevelDataMsg{
            .price_raw = level->price().raw(),
            .volume = level->total_volume(),
            .order_count = level->order_count(),
        };
    }

    // Asks depth
    const auto& asks = book.asks();
    size_t ask_count = std::min(asks.levels().size(), MAX_SNAPSHOT_LEVELS);
    snapshot.ask_level_count = static_cast<uint32_t>(ask_count);
    for (size_t i = 0; i < ask_count; ++i) {
        const PriceLevel* level = asks.levels()[i];
        snapshot.asks[i] = LevelDataMsg{
            .price_raw = level->price().raw(),
            .volume = level->total_volume(),
            .order_count = level->order_count(),
        };
    }

    if (sink_) {
        sink_->on_depth_snapshot(snapshot);
    }

    return snapshot;
}

void MarketDataPublisher::on_order_rested(const OrderRested& event) noexcept {
    AddOrderMsg msg{
        .msg_type = MarketDataMsgType::AddOrder,
        .sequence_number = ++sequence_number_,
        .order_id = event.order_id,
        .side = event.side,
        .price_raw = event.price.raw(),
        .volume = event.quantity,
        .timestamp = event.timestamp,
    };
    if (sink_) {
        sink_->on_add_order(msg);
    }
}

void MarketDataPublisher::on_trade(const TradeEvent& event) noexcept {
    OrderExecutedMsg msg{
        .msg_type = MarketDataMsgType::OrderExecuted,
        .sequence_number = ++sequence_number_,
        .order_id = event.maker_order_id,
        .match_id = event.match_id,
        .price_raw = event.price.raw(),
        .executed_volume = event.quantity,
        .timestamp = event.timestamp,
    };
    if (sink_) {
        sink_->on_order_executed(msg);
    }
}

void MarketDataPublisher::on_order_cancelled(const OrderCancelled& event) noexcept {
    OrderDeleteMsg msg{
        .msg_type = MarketDataMsgType::OrderDelete,
        .sequence_number = ++sequence_number_,
        .order_id = event.order_id,
        .timestamp = event.timestamp,
    };
    if (sink_) {
        sink_->on_order_deleted(msg);
    }
}

void MarketDataPublisher::on_order_filled(const OrderFilled& event) noexcept {
    (void)event;
}

void MarketDataPublisher::on_order_amended(const OrderAmended& event) noexcept {
    OrderReplaceMsg msg{
        .msg_type = MarketDataMsgType::OrderReplace,
        .sequence_number = ++sequence_number_,
        .order_id = event.order_id,
        .new_price_raw = event.new_price.raw(),
        .new_volume = event.new_quantity,
        .timestamp = event.timestamp,
    };
    if (sink_) {
        sink_->on_order_replaced(msg);
    }
}

}  // namespace orderbook
