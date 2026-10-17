#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "orderbook/alloc_guard.hpp"
#include "orderbook/feed_handler.hpp"
#include "orderbook/market_data_protocol.hpp"
#include "orderbook/market_data_publisher.hpp"
#include "orderbook/matching_engine.hpp"

using namespace orderbook;

namespace {

class CollectingSink : public IBinaryFeedSink {
public:
    std::vector<AddOrderMsg> adds;
    std::vector<OrderExecutedMsg> executions;
    std::vector<OrderCancelMsg> cancels;
    std::vector<OrderDeleteMsg> deletes;
    std::vector<DepthSnapshotMsg> snapshots;

    void on_add_order(const AddOrderMsg& msg) noexcept override { adds.push_back(msg); }
    void on_order_executed(const OrderExecutedMsg& msg) noexcept override {
        executions.push_back(msg);
    }
    void on_order_cancelled(const OrderCancelMsg& msg) noexcept override { cancels.push_back(msg); }
    void on_order_deleted(const OrderDeleteMsg& msg) noexcept override { deletes.push_back(msg); }
    void on_depth_snapshot(const DepthSnapshotMsg& msg) noexcept override {
        snapshots.push_back(msg);
    }

    void clear() {
        adds.clear();
        executions.clear();
        cancels.clear();
        deletes.clear();
        snapshots.clear();
    }
};

}  // namespace

TEST_CASE("MarketDataProtocol - Binary Layout and Endianness", "[market_data]") {
    REQUIRE(sizeof(AddOrderMsg) == 42);
    REQUIRE(sizeof(OrderExecutedMsg) == 49);
    REQUIRE(sizeof(OrderCancelMsg) == 33);
    REQUIRE(sizeof(OrderDeleteMsg) == 25);

    // Endianness byte swap round-trip
    uint64_t original64 = 0x123456789ABCDEF0ULL;
    uint64_t swapped64 = to_network_endian(original64);
    uint64_t restored64 = from_network_endian(swapped64);
    REQUIRE(restored64 == original64);

    uint32_t original32 = 0x1A2B3C4D;
    uint32_t swapped32 = to_network_endian(original32);
    uint32_t restored32 = from_network_endian(swapped32);
    REQUIRE(restored32 == original32);
}

TEST_CASE("MarketDataPublisher - L3 Incremental Feed Generation", "[market_data]") {
    OrderBook book(1024, 128);
    CollectingSink sink;
    MarketDataPublisher publisher(&sink);
    MatchingEngine engine(book, &publisher);

    // 1. Submit resting limit order
    REQUIRE(engine.submit_order(1, Side::Buy, Price::from_double(100.0), 50, 1000));
    REQUIRE(sink.adds.size() == 1);
    REQUIRE(sink.adds[0].sequence_number == 1);
    REQUIRE(sink.adds[0].order_id == 1);
    REQUIRE(sink.adds[0].volume == 50);
    REQUIRE(sink.adds[0].price_raw == Price::from_double(100.0).raw());

    // 2. Submit resting sell order
    REQUIRE(engine.submit_order(2, Side::Sell, Price::from_double(105.0), 30, 2000));
    REQUIRE(sink.adds.size() == 2);
    REQUIRE(sink.adds[1].sequence_number == 2);
    REQUIRE(sink.adds[1].order_id == 2);

    // 3. Crossing buy order (full fill of Order 2)
    REQUIRE(engine.submit_order(3, Side::Buy, Price::from_double(105.0), 30, 3000));
    REQUIRE(sink.executions.size() == 1);
    REQUIRE(sink.executions[0].sequence_number == 3);
    REQUIRE(sink.executions[0].order_id == 2);
    REQUIRE(sink.executions[0].executed_volume == 30);

    // 4. Cancel Order 1
    REQUIRE(engine.cancel_order(1, 4000));
    REQUIRE(sink.deletes.size() == 1);
    REQUIRE(sink.deletes[0].sequence_number == 4);
    REQUIRE(sink.deletes[0].order_id == 1);
}

TEST_CASE("MarketDataPublisher - L2 Market Depth Snapshots", "[market_data]") {
    OrderBook book(1024, 128);
    MarketDataPublisher publisher;

    // Build depth
    book.add_limit_order(1, Side::Buy, Price::from_double(99.0), 10, 1000);
    book.add_limit_order(2, Side::Buy, Price::from_double(98.0), 20, 1001);
    book.add_limit_order(3, Side::Buy, Price::from_double(97.0), 30, 1002);

    book.add_limit_order(4, Side::Sell, Price::from_double(101.0), 15, 1003);
    book.add_limit_order(5, Side::Sell, Price::from_double(102.0), 25, 1004);

    DepthSnapshotMsg snapshot = publisher.create_depth_snapshot(book, 5000);

    REQUIRE(snapshot.msg_type == MarketDataMsgType::DepthSnapshot);
    REQUIRE(snapshot.sequence_number == 1);
    REQUIRE(snapshot.bid_level_count == 3);
    REQUIRE(snapshot.ask_level_count == 2);

    // Best Bid: $99.0, vol 10
    REQUIRE(snapshot.bids[0].price_raw == Price::from_double(99.0).raw());
    REQUIRE(snapshot.bids[0].volume == 10);
    REQUIRE(snapshot.bids[1].price_raw == Price::from_double(98.0).raw());
    REQUIRE(snapshot.bids[1].volume == 20);

    // Best Ask: $101.0, vol 15
    REQUIRE(snapshot.asks[0].price_raw == Price::from_double(101.0).raw());
    REQUIRE(snapshot.asks[0].volume == 15);
    REQUIRE(snapshot.asks[1].price_raw == Price::from_double(102.0).raw());
    REQUIRE(snapshot.asks[1].volume == 25);
}

TEST_CASE("FeedHandler - Chaos Resilience and Sequence Gap Recovery", "[market_data]") {
    FeedHandler handler;
    REQUIRE(handler.is_in_sync());
    REQUIRE(handler.expected_sequence_number() == 0);

    // 1. Initial streaming packets 1 to 5
    for (uint64_t seq = 1; seq <= 5; ++seq) {
        AddOrderMsg msg{
            .msg_type = MarketDataMsgType::AddOrder,
            .sequence_number = seq,
            .order_id = seq,
            .side = Side::Buy,
            .price_raw = Price::from_double(100.0).raw(),
            .volume = 10,
            .timestamp = 1000 + seq,
        };
        REQUIRE(handler.on_add_order(msg));
    }

    REQUIRE(handler.expected_sequence_number() == 6);
    REQUIRE(!handler.has_detected_gaps());
    REQUIRE(handler.is_in_sync());

    // 2. CHAOS FAULT INJECTION: Network drops packets 6 through 15
    // Packet 16 arrives directly!
    AddOrderMsg msg16{
        .msg_type = MarketDataMsgType::AddOrder,
        .sequence_number = 16,
        .order_id = 16,
        .side = Side::Buy,
        .price_raw = Price::from_double(101.0).raw(),
        .volume = 20,
        .timestamp = 2000,
    };
    handler.on_add_order(msg16);

    // Verify Gap Detection
    REQUIRE(handler.has_detected_gaps());
    REQUIRE(handler.gap_count() == 1);
    REQUIRE(!handler.is_in_sync());
    REQUIRE(handler.detected_gaps().size() == 1);
    REQUIRE(handler.detected_gaps()[0].expected_seq == 6);
    REQUIRE(handler.detected_gaps()[0].received_seq == 16);
    REQUIRE(handler.detected_gaps()[0].missing_count == 10);
    REQUIRE(handler.expected_sequence_number() == 17);

    // 3. CHAOS FAULT INJECTION: Duplicate delivery of packet 16 arrives late
    handler.on_add_order(msg16);
    REQUIRE(handler.duplicate_count() == 1);

    // 4. RESYNCHRONIZATION RECOVERY: Apply authoritative L2 snapshot at sequence 25
    DepthSnapshotMsg recovery_snapshot{
        .msg_type = MarketDataMsgType::DepthSnapshot,
        .sequence_number = 25,
        .bid_level_count = 1,
        .ask_level_count = 1,
        .timestamp = 3000,
        .bids = {{.price_raw = Price::from_double(100.5).raw(), .volume = 100, .order_count = 2}},
        .asks = {{.price_raw = Price::from_double(101.5).raw(), .volume = 80, .order_count = 1}},
    };
    handler.apply_snapshot(recovery_snapshot);

    // Resynchronized!
    REQUIRE(handler.is_in_sync());
    REQUIRE(handler.expected_sequence_number() == 26);
    REQUIRE(handler.current_bbo().bid_price == Price::from_double(100.5));
    REQUIRE(handler.current_bbo().bid_volume == 100);
    REQUIRE(handler.current_bbo().ask_price == Price::from_double(101.5));
    REQUIRE(handler.current_bbo().ask_volume == 80);

    // Stream subsequent in-sync packet 26
    AddOrderMsg msg26{
        .msg_type = MarketDataMsgType::AddOrder,
        .sequence_number = 26,
        .order_id = 26,
        .side = Side::Buy,
        .price_raw = Price::from_double(100.6).raw(),
        .volume = 50,
        .timestamp = 3010,
    };
    REQUIRE(handler.on_add_order(msg26));
    REQUIRE(handler.is_in_sync());
    REQUIRE(handler.expected_sequence_number() == 27);
    REQUIRE(handler.current_bbo().bid_price == Price::from_double(100.6));
}

TEST_CASE("MarketData - Raw Binary Buffer Parsing via ProcessMessage", "[market_data]") {
    FeedHandler handler;

    AddOrderMsg original{
        .msg_type = MarketDataMsgType::AddOrder,
        .sequence_number = 1,
        .order_id = 42,
        .side = Side::Sell,
        .price_raw = Price::from_double(150.25).raw(),
        .volume = 300,
        .timestamp = 9999,
    };

    uint8_t buffer[64]{0};
    std::memcpy(buffer, &original, sizeof(AddOrderMsg));

    bool ok = handler.process_message(buffer, sizeof(AddOrderMsg));
    REQUIRE(ok);
    REQUIRE(handler.messages_processed() == 1);
    REQUIRE(handler.current_bbo().ask_price == Price::from_double(150.25));
    REQUIRE(handler.current_bbo().ask_volume == 300);
}

TEST_CASE("MarketData - Zero Dynamic Allocations on Hot Path", "[market_data]") {
    FeedHandler handler;

    AddOrderMsg msg{
        .msg_type = MarketDataMsgType::AddOrder,
        .sequence_number = 1,
        .order_id = 1,
        .side = Side::Buy,
        .price_raw = Price::from_double(100.0).raw(),
        .volume = 10,
        .timestamp = 1000,
    };

    uint8_t buffer[64]{0};
    std::memcpy(buffer, &msg, sizeof(AddOrderMsg));

    // Hot path starts: Zero allocations allowed
    {
        AllocationGuard guard;

        for (uint64_t i = 1; i <= 1000; ++i) {
            msg.sequence_number = i;
            msg.order_id = i;
            std::memcpy(buffer, &msg, sizeof(AddOrderMsg));
            handler.process_message(buffer, sizeof(AddOrderMsg));
        }
    }

    REQUIRE(handler.messages_processed() == 1000);
    REQUIRE(handler.is_in_sync());
}
