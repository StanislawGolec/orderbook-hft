#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <thread>

#include "orderbook/matching_engine.hpp"
#include "orderbook/spsc_queue.hpp"
#include "orderbook/types.hpp"

namespace orderbook {

enum class CommandType : uint8_t { NewOrder = 0, CancelOrder = 1, AmendOrder = 2 };

// Order command structure for the SPSC ingestion queue
struct OrderCommand {
    CommandType type{CommandType::NewOrder};
    OrderRequest request{};
    OrderId cancel_id{INVALID_ORDER_ID};
    AmendOrderRequest amend_request{};
    Timestamp timestamp{0};
};

static_assert(std::is_trivially_copyable_v<OrderCommand>,
              "OrderCommand must be trivially copyable");
static_assert(std::is_standard_layout_v<OrderCommand>, "OrderCommand must have standard layout");

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

// Single-Writer Engine Service running the matching engine on a dedicated pinned thread
class EngineService {
public:
    explicit EngineService(MatchingEngine& engine, size_t queue_capacity = 65536,
                           std::optional<uint32_t> core_affinity = std::nullopt);
    ~EngineService();

    EngineService(const EngineService&) = delete;
    EngineService& operator=(const EngineService&) = delete;
    EngineService(EngineService&&) = delete;
    EngineService& operator=(EngineService&&) = delete;

    void start();
    void stop();
    void join();

    [[nodiscard]] bool is_running() const noexcept;

    // Thread-safe lock-free non-blocking order submission (Single-Producer fast path)
    bool submit_order(const OrderRequest& request);

    bool submit_order(OrderId id, Side side, Price price, Volume volume, Timestamp ts,
                      OrderType type = OrderType::Limit, TimeInForce tif = TimeInForce::GTC) {
        return submit_order(OrderRequest{.id = id,
                                         .side = side,
                                         .type = type,
                                         .price = price,
                                         .volume = volume,
                                         .timestamp = ts,
                                         .tif = tif});
    }

    // Multi-Producer (MPSC) thread-safe order submission for concurrent client gateways
    bool submit_order_mpsc(const OrderRequest& request);

    bool submit_order_mpsc(OrderId id, Side side, Price price, Volume volume, Timestamp ts,
                           OrderType type = OrderType::Limit, TimeInForce tif = TimeInForce::GTC) {
        return submit_order_mpsc(OrderRequest{.id = id,
                                              .side = side,
                                              .type = type,
                                              .price = price,
                                              .volume = volume,
                                              .timestamp = ts,
                                              .tif = tif});
    }

    // Thread-safe lock-free non-blocking order cancellation (Single-Producer fast path)
    bool cancel_order(OrderId id, Timestamp ts = 0);

    // Multi-Producer (MPSC) thread-safe order cancellation
    bool cancel_order_mpsc(OrderId id, Timestamp ts = 0);

    // Thread-safe lock-free non-blocking order amendment (Single-Producer fast path)
    bool amend_order(const AmendOrderRequest& request);

    bool amend_order(OrderId id, Price new_price, Volume new_volume, Timestamp ts = 0) {
        return amend_order(AmendOrderRequest{
            .id = id, .new_price = new_price, .new_volume = new_volume, .timestamp = ts});
    }

    // Multi-Producer (MPSC) thread-safe order amendment
    bool amend_order_mpsc(const AmendOrderRequest& request);

    bool amend_order_mpsc(OrderId id, Price new_price, Volume new_volume, Timestamp ts = 0) {
        return amend_order_mpsc(AmendOrderRequest{
            .id = id, .new_price = new_price, .new_volume = new_volume, .timestamp = ts});
    }

    [[nodiscard]] size_t queue_size() const noexcept { return input_queue_.size(); }
    [[nodiscard]] bool queue_empty() const noexcept { return input_queue_.empty(); }

    [[nodiscard]] MatchingEngine& engine() noexcept { return engine_; }
    [[nodiscard]] const MatchingEngine& engine() const noexcept { return engine_; }

    [[nodiscard]] uint64_t processed_commands() const noexcept {
        return processed_commands_.load(std::memory_order_relaxed);
    }

private:
    void run();
    void process_command(const OrderCommand& cmd);

    MatchingEngine& engine_;
    SPSCQueue<OrderCommand> input_queue_;
    std::optional<uint32_t> core_affinity_;
    std::atomic<bool> running_{false};
    std::atomic<bool> is_running_{false};
    std::thread worker_thread_;
    std::atomic<uint64_t> processed_commands_{0};
    alignas(64) std::atomic_flag producer_lock_ = ATOMIC_FLAG_INIT;
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace orderbook
