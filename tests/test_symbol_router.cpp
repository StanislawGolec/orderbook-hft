#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

#include "orderbook/alloc_guard.hpp"
#include "orderbook/symbol_router.hpp"

using namespace orderbook;

TEST_CASE("SymbolId - Domain Modeling and Conversions", "[symbol_router]") {
    auto sym_aapl = SymbolId::from_string("AAPL");
    auto sym_msft = SymbolId::from_string("MSFT");
    auto sym_empty = SymbolId();

    CHECK(!sym_aapl.empty());
    CHECK(!sym_msft.empty());
    CHECK(sym_empty.empty());

    CHECK(sym_aapl.to_string() == "AAPL");
    CHECK(sym_msft.to_string() == "MSFT");

    CHECK(sym_aapl == SymbolId::from_string("AAPL"));
    CHECK(sym_aapl != sym_msft);
    CHECK((sym_aapl < sym_msft || sym_aapl > sym_msft));

    // Full 8-byte symbol (e.g. currency pair or crypto)
    auto sym_eurusd = SymbolId::from_string("EUR/USD");
    CHECK(sym_eurusd.to_string() == "EUR/USD");
    CHECK(sizeof(SymbolId) == 8);
}

TEST_CASE("SymbolRouter - Symbol Registration and Lookup", "[symbol_router]") {
    SymbolRouter router;

    OrderBook book_aapl(1000, 100);
    MatchingEngine engine_aapl(book_aapl);
    EngineService service_aapl(engine_aapl, 1024);

    OrderBook book_msft(1000, 100);
    MatchingEngine engine_msft(book_msft);
    EngineService service_msft(engine_msft, 1024);

    auto sym_aapl = SymbolId::from_string("AAPL");
    auto sym_msft = SymbolId::from_string("MSFT");

    REQUIRE(router.register_symbol(sym_aapl, &service_aapl));
    REQUIRE(router.register_symbol(sym_msft, &service_msft));
    CHECK(router.symbol_count() == 2);

    // Duplicate registration rejected
    CHECK(!router.register_symbol(sym_aapl, &service_aapl));
    CHECK(router.symbol_count() == 2);

    // Null service or empty symbol rejected
    CHECK(!router.register_symbol(SymbolId(), &service_aapl));
    CHECK(!router.register_symbol(SymbolId::from_string("GOOG"), nullptr));

    CHECK(router.contains(sym_aapl));
    CHECK(router.contains(sym_msft));
    CHECK(!router.contains(SymbolId::from_string("NVDA")));

    CHECK(router.get_service(sym_aapl) == &service_aapl);
    CHECK(router.get_service(sym_msft) == &service_msft);
    CHECK(router.get_service(SymbolId::from_string("NVDA")) == nullptr);
}

TEST_CASE("SymbolRouter - Order Submission, Amend, and Cancel Routing", "[symbol_router]") {
    SymbolRouter router;

    OrderBook book_aapl(1000, 100);
    MatchingEngine engine_aapl(book_aapl);
    EngineService service_aapl(engine_aapl, 1024);

    OrderBook book_msft(1000, 100);
    MatchingEngine engine_msft(book_msft);
    EngineService service_msft(engine_msft, 1024);

    auto sym_aapl = SymbolId::from_string("AAPL");
    auto sym_msft = SymbolId::from_string("MSFT");

    router.register_symbol(sym_aapl, &service_aapl);
    router.register_symbol(sym_msft, &service_msft);

    router.start_all();

    Price p_aapl = Price::from_double(150.0);
    Price p_msft = Price::from_double(300.0);

    // Submit orders to routed symbols
    REQUIRE(router.submit_order(sym_aapl, 1001, Side::Buy, p_aapl, 50, 1000));
    REQUIRE(router.submit_order(sym_msft, 2001, Side::Buy, p_msft, 100, 1001));

    // Amend AAPL order volume
    REQUIRE(router.amend_order(sym_aapl, 1001, p_aapl, 30, 1002));

    // Cancel MSFT order
    REQUIRE(router.cancel_order(sym_msft, 2001, 1003));

    router.stop_all();
    router.join_all();

    // Verify AAPL book state
    CHECK(service_aapl.processed_commands() == 2);
    Order* o_aapl = book_aapl.find_order(1001);
    REQUIRE(o_aapl != nullptr);
    CHECK(o_aapl->remaining_volume == 30);
    CHECK(book_aapl.best_bid_volume() == 30);

    // Verify MSFT book state
    CHECK(service_msft.processed_commands() == 2);
    CHECK(book_msft.find_order(2001) == nullptr);
    CHECK(book_msft.is_empty());
}

TEST_CASE("SymbolRouter - Consolidated BBO Queries", "[symbol_router]") {
    SymbolRouter router;

    OrderBook book_aapl(1000, 100);
    MatchingEngine engine_aapl(book_aapl);
    EngineService service_aapl(engine_aapl, 1024);

    OrderBook book_msft(1000, 100);
    MatchingEngine engine_msft(book_msft);
    EngineService service_msft(engine_msft, 1024);

    auto sym_aapl = SymbolId::from_string("AAPL");
    auto sym_msft = SymbolId::from_string("MSFT");

    router.register_symbol(sym_aapl, &service_aapl);
    router.register_symbol(sym_msft, &service_msft);

    router.start_all();

    Price bid_aapl = Price::from_double(150.0);
    Price ask_aapl = Price::from_double(150.50);
    Price bid_msft = Price::from_double(300.0);
    Price ask_msft = Price::from_double(301.0);

    router.submit_order(sym_aapl, 1, Side::Buy, bid_aapl, 100, 10);
    router.submit_order(sym_aapl, 2, Side::Sell, ask_aapl, 200, 20);

    router.submit_order(sym_msft, 3, Side::Buy, bid_msft, 300, 30);
    router.submit_order(sym_msft, 4, Side::Sell, ask_msft, 400, 40);

    router.stop_all();
    router.join_all();

    // Query single symbol BBO
    auto bbo_aapl = router.get_bbo(sym_aapl);
    REQUIRE(bbo_aapl.has_value());
    CHECK(bbo_aapl->best_bid == bid_aapl);
    CHECK(bbo_aapl->best_bid_volume == 100);
    CHECK(bbo_aapl->best_ask == ask_aapl);
    CHECK(bbo_aapl->best_ask_volume == 200);

    auto bbo_msft = router.get_bbo(sym_msft);
    REQUIRE(bbo_msft.has_value());
    CHECK(bbo_msft->best_bid == bid_msft);
    CHECK(bbo_msft->best_bid_volume == 300);
    CHECK(bbo_msft->best_ask == ask_msft);
    CHECK(bbo_msft->best_ask_volume == 400);

    // Query non-existent symbol
    CHECK(!router.get_bbo(SymbolId::from_string("TSLA")).has_value());

    // Consolidated all-BBO retrieval
    auto all_bbo = router.get_all_bbo();
    REQUIRE(all_bbo.size() == 2);
    CHECK(all_bbo[0].symbol == sym_aapl);
    CHECK(all_bbo[1].symbol == sym_msft);
}

TEST_CASE("SymbolRouter - Multi-Client Concurrent Stress and Isolation", "[symbol_router]") {
    SymbolRouter router;

    constexpr size_t NUM_SYMBOLS = 4;
    const std::vector<SymbolId> symbols = {
        SymbolId::from_string("AAPL"),
        SymbolId::from_string("MSFT"),
        SymbolId::from_string("GOOG"),
        SymbolId::from_string("NVDA"),
    };

    std::vector<std::unique_ptr<OrderBook>> books;
    std::vector<std::unique_ptr<MatchingEngine>> engines;
    std::vector<std::unique_ptr<EngineService>> services;

    for (size_t i = 0; i < NUM_SYMBOLS; ++i) {
        books.push_back(std::make_unique<OrderBook>(50000, 1000));
        engines.push_back(std::make_unique<MatchingEngine>(*books[i]));
        services.push_back(std::make_unique<EngineService>(*engines[i], 32768));
        REQUIRE(router.register_symbol(symbols[i], services[i].get()));
    }

    router.start_all();

    constexpr size_t ORDERS_PER_PRODUCER = 10000;
    std::atomic<bool> start_signal{false};

    auto producer_fn = [&](SymbolId sym, uint64_t base_id) {
        while (!start_signal.load(std::memory_order_relaxed)) {
            // wait for sync start
        }
        Price base_p = Price::from_double(100.0);
        for (uint64_t i = 0; i < ORDERS_PER_PRODUCER; ++i) {
            uint64_t order_id = base_id + i;
            Price p = base_p + Price::from_raw(static_cast<int64_t>(i % 50));
            while (!router.submit_order(sym, order_id, Side::Buy, p, 10, i)) {
                // SPSC full backoff
                std::this_thread::yield();
            }
        }
    };

    std::vector<std::thread> producers;
    for (size_t i = 0; i < NUM_SYMBOLS; ++i) {
        producers.emplace_back(producer_fn, symbols[i], (i + 1) * 1000000);
    }

    start_signal.store(true, std::memory_order_release);

    for (auto& t : producers) {
        t.join();
    }

    router.stop_all();
    router.join_all();

    for (size_t i = 0; i < NUM_SYMBOLS; ++i) {
        CHECK(services[i]->processed_commands() == ORDERS_PER_PRODUCER);
        CHECK(books[i]->order_count() == ORDERS_PER_PRODUCER);
    }
}

TEST_CASE("SymbolRouter - Zero Dynamic Heap Allocations on Routing Hot Path", "[symbol_router]") {
    SymbolRouter router;

    OrderBook book_aapl(5000, 500);
    book_aapl.preallocate(5000, 500);
    MatchingEngine engine_aapl(book_aapl);
    EngineService service_aapl(engine_aapl, 4096);

    auto sym_aapl = SymbolId::from_string("AAPL");
    router.register_symbol(sym_aapl, &service_aapl);

    Price p = Price::from_double(150.0);

    bool all_submitted = true;
    bool all_amended = true;
    bool all_cancelled = true;
    {
        AllocationGuard guard;

        for (uint64_t id = 1; id <= 100; ++id) {
            // Hot-path order routing: MUST NOT ALLOCATE
            if (!router.submit_order(sym_aapl, id, Side::Buy, p, 50, id)) {
                all_submitted = false;
            }
        }

        for (uint64_t id = 1; id <= 50; ++id) {
            if (!router.amend_order(sym_aapl, id, p, 25, id + 100)) {
                all_amended = false;
            }
        }

        for (uint64_t id = 51; id <= 100; ++id) {
            if (!router.cancel_order(sym_aapl, id, id + 200)) {
                all_cancelled = false;
            }
        }
    }

    CHECK(all_submitted);
    CHECK(all_amended);
    CHECK(all_cancelled);

    SUCCEED("Dispatched 200 operations across SymbolRouter with 0 dynamic heap allocations");
}

TEST_CASE("SymbolRouter - Multi-Producer Same-Symbol MPSC and Concurrent BBO Queries",
          "[symbol_router]") {
    SymbolRouter router;
    auto sym_aapl = SymbolId::from_string("AAPL");

    OrderBook book_aapl(50000, 1000);
    MatchingEngine engine_aapl(book_aapl);
    EngineService service_aapl(engine_aapl, 32768);
    REQUIRE(router.register_symbol(sym_aapl, &service_aapl));

    router.start_all();

    constexpr size_t NUM_PRODUCERS = 4;
    constexpr size_t ORDERS_PER_THREAD = 2500;
    std::atomic<bool> start_signal{false};
    std::atomic<bool> producers_done{false};

    std::vector<std::thread> producers;
    for (size_t t = 0; t < NUM_PRODUCERS; ++t) {
        producers.emplace_back([&, t]() {
            while (!start_signal.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            Price base_p = Price::from_double(150.0);
            uint64_t base_id = (t + 1) * 100000;
            for (uint64_t i = 0; i < ORDERS_PER_THREAD; ++i) {
                Side s = (i % 2 == 0) ? Side::Buy : Side::Sell;
                Price offset = Price::from_raw(static_cast<int64_t>((i % 20) + 1) * 100);
                Price p = (s == Side::Buy) ? (base_p - offset) : (base_p + offset);
                while (!router.submit_order_mpsc(sym_aapl, base_id + i, s, p, 10, i)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    // Concurrent BBO reader thread polling lock-free atomic BBO while levels are mutated
    std::atomic<uint64_t> bbo_reads{0};
    std::thread bbo_reader([&]() {
        while (!start_signal.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        while (!producers_done.load(std::memory_order_relaxed)) {
            auto bbo = router.get_bbo(sym_aapl);
            if (bbo.has_value()) {
                bbo_reads.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    start_signal.store(true, std::memory_order_release);

    for (auto& p : producers) {
        p.join();
    }
    producers_done.store(true, std::memory_order_release);
    bbo_reader.join();

    router.stop_all();
    router.join_all();

    CHECK(service_aapl.processed_commands() == NUM_PRODUCERS * ORDERS_PER_THREAD);
    CHECK(book_aapl.order_count() == NUM_PRODUCERS * ORDERS_PER_THREAD);
}
