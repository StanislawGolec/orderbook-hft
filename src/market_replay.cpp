#include "orderbook/market_replay.hpp"

#include <chrono>
#include <random>
#include <vector>

#include "orderbook/invariant_checker.hpp"

namespace orderbook {

namespace {

class ReplayListener : public IMatchingListener {
public:
    explicit ReplayListener(IMatchingListener* next = nullptr) : next_(next) {}

    void on_trade(const TradeEvent& event) noexcept override {
        ++trade_count;
        volume_traded += event.quantity;
        if (next_) {
            next_->on_trade(event);
        }
    }
    void on_order_accepted(const OrderAccepted& event) noexcept override {
        if (next_) {
            next_->on_order_accepted(event);
        }
    }
    void on_order_rested(const OrderRested& event) noexcept override {
        if (next_) {
            next_->on_order_rested(event);
        }
    }
    void on_order_rejected(const OrderRejected& event) noexcept override {
        if (next_) {
            next_->on_order_rejected(event);
        }
    }
    void on_order_cancelled(const OrderCancelled& event) noexcept override {
        if (next_) {
            next_->on_order_cancelled(event);
        }
    }
    void on_order_filled(const OrderFilled& event) noexcept override {
        if (next_) {
            next_->on_order_filled(event);
        }
    }
    void on_order_amended(const OrderAmended& event) noexcept override {
        if (next_) {
            next_->on_order_amended(event);
        }
    }

    size_t trade_count{0};
    Volume volume_traded{0};
    IMatchingListener* next_{nullptr};
};

}  // namespace

MarketReplaySimulator::MarketReplaySimulator(const ReplayConfig& config) : config_(config) {
    active_order_ids_.reserve(config_.total_operations);
}

ReplayStats MarketReplaySimulator::run(OrderBook& book, MatchingEngine& engine,
                                       LatencyProfiler* profiler) {
    ReplayStats stats{};
    stats.total_operations = config_.total_operations;

    IMatchingListener* prev_listener = engine.listener();
    ReplayListener replay_listener(prev_listener);
    engine.set_listener(&replay_listener);

    active_order_ids_.clear();

    std::mt19937_64 rng(config_.random_seed);
    std::uniform_real_distribution<double> action_dist(0.0, 1.0);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int64_t> price_offset_dist(-50, 50);
    std::uniform_int_distribution<uint64_t> volume_dist(10, 100);

    const double limit_threshold = config_.limit_order_ratio;
    const double market_threshold = limit_threshold + config_.market_order_ratio;
    const double cancel_threshold = market_threshold + config_.cancel_order_ratio;

    OrderId next_order_id = 1;
    Timestamp sim_time = 1000;

    auto start_time = std::chrono::steady_clock::now();

    for (size_t op = 0; op < config_.total_operations; ++op) {
        double roll = action_dist(rng);
        Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
        ++sim_time;

        if (roll < limit_threshold) {
            // 1. Limit order insertion clustered around BBO
            Price ref_price = config_.initial_price;
            if (side == Side::Buy && book.has_bids()) {
                ref_price = book.best_bid();
            } else if (side == Side::Sell && book.has_asks()) {
                ref_price = book.best_ask();
            }

            int64_t offset = price_offset_dist(rng);
            Price limit_p = ref_price + Price::from_raw(offset);
            if (limit_p <= Price::zero()) {
                limit_p = Price::from_raw(10000);  // $1.00 minimum
            }

            Volume vol = volume_dist(rng);
            OrderId id = next_order_id++;

            uint64_t t0 = 0;
            if (profiler) {
                t0 = rdtsc_probe();
            }

            bool submitted = engine.submit_order(id, side, limit_p, vol, sim_time);

            if (profiler) {
                uint64_t t1 = rdtsc_probe();
                profiler->record_cycles(t1 - t0);
            }

            if (submitted && book.find_order(id) != nullptr) {
                active_order_ids_.push_back(id);
            }
            ++stats.limit_orders;
        } else if (roll < market_threshold) {
            // 2. Aggressive Market order sweep
            bool has_opposing = (side == Side::Buy) ? book.has_asks() : book.has_bids();
            if (has_opposing) {
                Volume vol = volume_dist(rng);
                OrderId id = next_order_id++;

                uint64_t t0 = 0;
                if (profiler) {
                    t0 = rdtsc_probe();
                }

                engine.submit_order(id, side, Price::zero(), vol, sim_time, OrderType::Market);

                if (profiler) {
                    uint64_t t1 = rdtsc_probe();
                    profiler->record_cycles(t1 - t0);
                }
                ++stats.market_orders;
            } else {
                // If opposing book empty, submit limit order instead
                Price limit_p = config_.initial_price;
                Volume vol = volume_dist(rng);
                OrderId id = next_order_id++;
                if (engine.submit_order(id, side, limit_p, vol, sim_time)) {
                    if (book.find_order(id) != nullptr) {
                        active_order_ids_.push_back(id);
                    }
                }
                ++stats.limit_orders;
            }
        } else if (roll < cancel_threshold) {
            // 3. Cancellation of existing resting order
            if (!active_order_ids_.empty()) {
                std::uniform_int_distribution<size_t> idx_dist(0, active_order_ids_.size() - 1);
                size_t idx = idx_dist(rng);
                OrderId cancel_id = active_order_ids_[idx];

                // Fast O(1) removal from active array
                active_order_ids_[idx] = active_order_ids_.back();
                active_order_ids_.pop_back();

                uint64_t t0 = 0;
                if (profiler) {
                    t0 = rdtsc_probe();
                }

                engine.cancel_order(cancel_id, sim_time);

                if (profiler) {
                    uint64_t t1 = rdtsc_probe();
                    profiler->record_cycles(t1 - t0);
                }
                ++stats.cancellations;
            }
        } else {
            // 4. In-Place Order Amendment
            if (!active_order_ids_.empty()) {
                std::uniform_int_distribution<size_t> idx_dist(0, active_order_ids_.size() - 1);
                size_t idx = idx_dist(rng);
                OrderId amend_id = active_order_ids_[idx];
                Order* o = book.find_order(amend_id);

                if (o != nullptr) {
                    Volume new_vol = volume_dist(rng);
                    Price new_p = o->price;
                    if (roll > 0.95) {
                        // 5% chance of price amend
                        new_p = new_p + Price::from_raw(price_offset_dist(rng));
                        if (new_p <= Price::zero()) {
                            new_p = o->price;
                        }
                    }

                    uint64_t t0 = 0;
                    if (profiler) {
                        t0 = rdtsc_probe();
                    }

                    engine.amend_order(amend_id, new_p, new_vol, sim_time);

                    if (profiler) {
                        uint64_t t1 = rdtsc_probe();
                        profiler->record_cycles(t1 - t0);
                    }

                    if (book.find_order(amend_id) == nullptr) {
                        active_order_ids_[idx] = active_order_ids_.back();
                        active_order_ids_.pop_back();
                    }
                    ++stats.amendments;
                }
            }
        }

        // Periodic invariant audit during soak execution
        if (config_.invariant_check_interval > 0 &&
            (op + 1) % config_.invariant_check_interval == 0) {
            if (!InvariantChecker::check_no_crossed_book(book)) {
                stats.invariant_audit_passed = false;
            }
            auto report = InvariantChecker::check_book_consistency(book);
            if (!report.passed) {
                stats.invariant_audit_passed = false;
            }
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    stats.elapsed_seconds = std::chrono::duration<double>(end_time - start_time).count();
    if (stats.elapsed_seconds > 0.0) {
        stats.ops_per_second =
            static_cast<double>(config_.total_operations) / stats.elapsed_seconds;
    }

    // Final deep invariant audit
    if (!InvariantChecker::check_no_crossed_book(book)) {
        stats.invariant_audit_passed = false;
    }
    auto final_report = InvariantChecker::check_book_consistency(book);
    if (!final_report.passed) {
        stats.invariant_audit_passed = false;
    }

    stats.trades = replay_listener.trade_count;
    stats.total_volume_traded = replay_listener.volume_traded;

    engine.set_listener(prev_listener);
    return stats;
}

}  // namespace orderbook
