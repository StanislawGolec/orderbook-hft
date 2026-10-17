#include "orderbook/symbol_router.hpp"

namespace orderbook {

bool SymbolRouter::register_symbol(SymbolId symbol, EngineService* service) {
    if (symbol.empty() || service == nullptr || count_ >= MAX_ROUTER_SYMBOLS) {
        return false;
    }

    for (size_t i = 0; i < count_; ++i) {
        if (entries_[i].symbol == symbol) {
            return false;
        }
    }

    entries_[count_] = Entry{.symbol = symbol, .service = service};
    ++count_;
    return true;
}

EngineService* SymbolRouter::get_service(SymbolId symbol) const noexcept {
    for (size_t i = 0; i < count_; ++i) {
        if (entries_[i].symbol == symbol) [[likely]] {
            return entries_[i].service;
        }
    }
    return nullptr;
}

bool SymbolRouter::contains(SymbolId symbol) const noexcept {
    return get_service(symbol) != nullptr;
}

bool SymbolRouter::submit_order(SymbolId symbol, const OrderRequest& request) {
    EngineService* service = get_service(symbol);
    if (!service) [[unlikely]] {
        return false;
    }
    return service->submit_order(request);
}

bool SymbolRouter::cancel_order(SymbolId symbol, OrderId id, Timestamp ts) {
    EngineService* service = get_service(symbol);
    if (!service) [[unlikely]] {
        return false;
    }
    return service->cancel_order(id, ts);
}

bool SymbolRouter::amend_order(SymbolId symbol, const AmendOrderRequest& request) {
    EngineService* service = get_service(symbol);
    if (!service) [[unlikely]] {
        return false;
    }
    return service->amend_order(request);
}

void SymbolRouter::start_all() {
    for (size_t i = 0; i < count_; ++i) {
        if (!entries_[i].service->is_running()) {
            entries_[i].service->start();
        }
    }
}

void SymbolRouter::stop_all() {
    for (size_t i = 0; i < count_; ++i) {
        entries_[i].service->stop();
    }
}

void SymbolRouter::join_all() {
    for (size_t i = 0; i < count_; ++i) {
        entries_[i].service->join();
    }
}

std::optional<ConsolidatedBBO> SymbolRouter::get_bbo(SymbolId symbol) const noexcept {
    EngineService* service = get_service(symbol);
    if (!service) {
        return std::nullopt;
    }

    const OrderBook& book = service->engine().book();
    return ConsolidatedBBO{
        .symbol = symbol,
        .best_bid = book.best_bid(),
        .best_bid_volume = book.best_bid_volume(),
        .best_ask = book.best_ask(),
        .best_ask_volume = book.best_ask_volume(),
    };
}

std::vector<ConsolidatedBBO> SymbolRouter::get_all_bbo() const {
    std::vector<ConsolidatedBBO> result;
    result.reserve(count_);
    for (size_t i = 0; i < count_; ++i) {
        const OrderBook& book = entries_[i].service->engine().book();
        result.push_back(ConsolidatedBBO{
            .symbol = entries_[i].symbol,
            .best_bid = book.best_bid(),
            .best_bid_volume = book.best_bid_volume(),
            .best_ask = book.best_ask(),
            .best_ask_volume = book.best_ask_volume(),
        });
    }
    return result;
}

}  // namespace orderbook
