#include "orderbook/engine_service.hpp"

#include "orderbook/cpu_affinity.hpp"
#include "orderbook/cpu_pause.hpp"

namespace orderbook {

EngineService::EngineService(MatchingEngine& engine, size_t queue_capacity,
                             std::optional<uint32_t> core_affinity)
    : engine_(engine), input_queue_(queue_capacity), core_affinity_(core_affinity) {}

EngineService::~EngineService() {
    stop();
    join();
}

void EngineService::start() {
    if (is_running_.load(std::memory_order_acquire)) {
        return;
    }

    running_.store(true, std::memory_order_release);
    worker_thread_ = std::thread(&EngineService::run, this);

    while (!is_running_.load(std::memory_order_acquire)) {
        cpu_pause();
    }
}

void EngineService::stop() {
    running_.store(false, std::memory_order_release);
}

void EngineService::join() {
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

bool EngineService::is_running() const noexcept {
    return is_running_.load(std::memory_order_acquire);
}

bool EngineService::submit_order(const OrderRequest& request) {
    OrderCommand cmd{.type = CommandType::NewOrder,
                     .request = request,
                     .cancel_id = INVALID_ORDER_ID,
                     .timestamp = request.timestamp};
    return input_queue_.try_push(cmd);
}

bool EngineService::cancel_order(OrderId id, Timestamp ts) {
    OrderCommand cmd{
        .type = CommandType::CancelOrder, .request = {}, .cancel_id = id, .timestamp = ts};
    return input_queue_.try_push(cmd);
}

void EngineService::run() {
    if (core_affinity_.has_value()) {
        pin_current_thread_to_core(*core_affinity_);
        set_thread_high_priority();
    }

    is_running_.store(true, std::memory_order_release);

    while (running_.load(std::memory_order_relaxed)) {
        OrderCommand cmd;
        if (input_queue_.try_pop(cmd)) {
            process_command(cmd);
            ++processed_commands_;
        } else {
            cpu_pause();
        }
    }

    // Drain remaining items after stop() is requested
    OrderCommand cmd;
    while (input_queue_.try_pop(cmd)) {
        process_command(cmd);
        ++processed_commands_;
    }

    is_running_.store(false, std::memory_order_release);
}

void EngineService::process_command(const OrderCommand& cmd) {
    if (cmd.type == CommandType::NewOrder) {
        engine_.process_order(cmd.request);
    } else if (cmd.type == CommandType::CancelOrder) {
        engine_.cancel_order(cmd.cancel_id, cmd.timestamp);
    }
}

}  // namespace orderbook
