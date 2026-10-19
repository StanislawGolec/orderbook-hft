#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "orderbook/engine_service.hpp"
#include "orderbook/events.hpp"
#include "orderbook/matching_engine.hpp"
#include "orderbook/price.hpp"
#include "orderbook/types.hpp"

namespace orderbook {

// Top-of-book consolidated quote for a given instrument
struct ConsolidatedBBO {
    SymbolId symbol{};
    Price best_bid{Price::zero()};
    Volume best_bid_volume{0};
    Price best_ask{Price::zero()};
    Volume best_ask_volume{0};
};

inline constexpr size_t MAX_ROUTER_SYMBOLS = 64;

// Multi-Instrument Gateway routing orders across isolated, core-pinned engine services
class SymbolRouter {
public:
    SymbolRouter() = default;
    ~SymbolRouter() = default;

    SymbolRouter(const SymbolRouter&) = delete;
    SymbolRouter& operator=(const SymbolRouter&) = delete;
    SymbolRouter(SymbolRouter&&) = delete;
    SymbolRouter& operator=(SymbolRouter&&) = delete;

    // Registers an engine service for a specific ticker symbol
    bool register_symbol(SymbolId symbol, EngineService* service);

    // Queries registered engine service for a symbol
    [[nodiscard]] EngineService* get_service(SymbolId symbol) const noexcept;

    // Fast order submission routed directly to the target symbol's lock-free SPSC queue
    bool submit_order(SymbolId symbol, const OrderRequest& request);

    bool submit_order(SymbolId symbol, OrderId id, Side side, Price price, Volume volume,
                      Timestamp ts, OrderType type = OrderType::Limit,
                      TimeInForce tif = TimeInForce::GTC) {
        return submit_order(symbol, OrderRequest{.id = id,
                                                 .side = side,
                                                 .type = type,
                                                 .price = price,
                                                 .volume = volume,
                                                 .timestamp = ts,
                                                 .tif = tif});
    }

    // Multi-Producer (MPSC) thread-safe order submission for concurrent clients targeting same symbol
    bool submit_order_mpsc(SymbolId symbol, const OrderRequest& request);

    bool submit_order_mpsc(SymbolId symbol, OrderId id, Side side, Price price, Volume volume,
                           Timestamp ts, OrderType type = OrderType::Limit,
                           TimeInForce tif = TimeInForce::GTC) {
        return submit_order_mpsc(symbol, OrderRequest{.id = id,
                                                      .side = side,
                                                      .type = type,
                                                      .price = price,
                                                      .volume = volume,
                                                      .timestamp = ts,
                                                      .tif = tif});
    }

    // Fast order cancellation routed to target symbol
    bool cancel_order(SymbolId symbol, OrderId id, Timestamp ts = 0);

    // Fast order amendment routed to target symbol
    bool amend_order(SymbolId symbol, const AmendOrderRequest& request);

    bool amend_order(SymbolId symbol, OrderId id, Price new_price, Volume new_volume,
                     Timestamp ts = 0) {
        return amend_order(
            symbol,
            AmendOrderRequest{
                .id = id, .new_price = new_price, .new_volume = new_volume, .timestamp = ts});
    }

    // Lifecycle coordination for all registered engine services
    void start_all();
    void stop_all();
    void join_all();

    [[nodiscard]] size_t symbol_count() const noexcept { return count_; }
    [[nodiscard]] bool contains(SymbolId symbol) const noexcept;

    // Consolidated market data queries
    [[nodiscard]] std::optional<ConsolidatedBBO> get_bbo(SymbolId symbol) const noexcept;
    [[nodiscard]] std::vector<ConsolidatedBBO> get_all_bbo() const;

private:
    struct Entry {
        SymbolId symbol{};
        EngineService* service{nullptr};
    };

    std::array<Entry, MAX_ROUTER_SYMBOLS> entries_{};
    size_t count_{0};
};

}  // namespace orderbook
