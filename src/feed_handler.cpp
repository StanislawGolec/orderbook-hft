#include "orderbook/feed_handler.hpp"

#include <cstring>

namespace orderbook {

bool FeedHandler::check_sequence(uint64_t seq) {
    if (expected_sequence_number_ == 0) {
        expected_sequence_number_ = seq + 1;
        in_sync_ = true;
        return true;
    }

    if (seq < expected_sequence_number_) {
        ++duplicate_count_;
        return false;
    }

    if (seq > expected_sequence_number_) {
        uint64_t missing = seq - expected_sequence_number_;
        detected_gaps_.push_back(SequenceGap{.expected_seq = expected_sequence_number_,
                                             .received_seq = seq,
                                             .missing_count = missing});
        ++gap_count_;
        in_sync_ = false;
        expected_sequence_number_ = seq + 1;
        return true;
    }

    ++expected_sequence_number_;
    return true;
}

bool FeedHandler::process_message(const uint8_t* buffer, size_t length) {
    if (!buffer || length == 0) {
        return false;
    }

    auto msg_type = static_cast<MarketDataMsgType>(buffer[0]);

    switch (msg_type) {
        case MarketDataMsgType::AddOrder: {
            if (length < sizeof(AddOrderMsg)) {
                return false;
            }
            AddOrderMsg msg;
            std::memcpy(&msg, buffer, sizeof(AddOrderMsg));
            return on_add_order(msg);
        }
        case MarketDataMsgType::OrderExecuted: {
            if (length < sizeof(OrderExecutedMsg)) {
                return false;
            }
            OrderExecutedMsg msg;
            std::memcpy(&msg, buffer, sizeof(OrderExecutedMsg));
            return on_order_executed(msg);
        }
        case MarketDataMsgType::OrderCancel: {
            if (length < sizeof(OrderCancelMsg)) {
                return false;
            }
            OrderCancelMsg msg;
            std::memcpy(&msg, buffer, sizeof(OrderCancelMsg));
            return on_order_cancelled(msg);
        }
        case MarketDataMsgType::OrderDelete: {
            if (length < sizeof(OrderDeleteMsg)) {
                return false;
            }
            OrderDeleteMsg msg;
            std::memcpy(&msg, buffer, sizeof(OrderDeleteMsg));
            return on_order_deleted(msg);
        }
        case MarketDataMsgType::DepthSnapshot: {
            if (length < sizeof(DepthSnapshotMsg)) {
                return false;
            }
            DepthSnapshotMsg msg;
            std::memcpy(&msg, buffer, sizeof(DepthSnapshotMsg));
            return on_depth_snapshot(msg);
        }
        default:
            return false;
    }
}

bool FeedHandler::on_add_order(const AddOrderMsg& msg) {
    check_sequence(msg.sequence_number);
    ++messages_processed_;

    Price p = Price::from_raw(msg.price_raw);
    if (msg.side == Side::Buy) {
        if (p > bbo_.bid_price || bbo_.bid_price == Price::zero()) {
            bbo_.bid_price = p;
            bbo_.bid_volume = msg.volume;
        }
    } else {
        if (p < bbo_.ask_price || bbo_.ask_price == Price::zero()) {
            bbo_.ask_price = p;
            bbo_.ask_volume = msg.volume;
        }
    }

    return true;
}

bool FeedHandler::on_order_executed(const OrderExecutedMsg& msg) {
    check_sequence(msg.sequence_number);
    ++messages_processed_;
    return true;
}

bool FeedHandler::on_order_cancelled(const OrderCancelMsg& msg) {
    check_sequence(msg.sequence_number);
    ++messages_processed_;
    return true;
}

bool FeedHandler::on_order_deleted(const OrderDeleteMsg& msg) {
    check_sequence(msg.sequence_number);
    ++messages_processed_;
    return true;
}

bool FeedHandler::on_depth_snapshot(const DepthSnapshotMsg& msg) {
    apply_snapshot(msg);
    ++messages_processed_;
    return true;
}

void FeedHandler::apply_snapshot(const DepthSnapshotMsg& snapshot) {
    expected_sequence_number_ = snapshot.sequence_number + 1;
    in_sync_ = true;

    if (snapshot.bid_level_count > 0) {
        bbo_.bid_price = Price::from_raw(snapshot.bids[0].price_raw);
        bbo_.bid_volume = snapshot.bids[0].volume;
    } else {
        bbo_.bid_price = Price::zero();
        bbo_.bid_volume = 0;
    }

    if (snapshot.ask_level_count > 0) {
        bbo_.ask_price = Price::from_raw(snapshot.asks[0].price_raw);
        bbo_.ask_volume = snapshot.asks[0].volume;
    } else {
        bbo_.ask_price = Price::zero();
        bbo_.ask_volume = 0;
    }
}

void FeedHandler::reset() noexcept {
    expected_sequence_number_ = 0;
    messages_processed_ = 0;
    gap_count_ = 0;
    duplicate_count_ = 0;
    in_sync_ = true;
    detected_gaps_.clear();
    bbo_ = BBOQuote{};
}

}  // namespace orderbook
