include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
    GIT_SHALLOW TRUE
)

set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(Catch2)

add_executable(orderbook_tests
    test_types.cpp
    test_price.cpp
    test_events.cpp
    test_order.cpp
    test_intrusive_list.cpp
    test_memory_pool.cpp
    test_alloc_guard.cpp
    test_price_level.cpp
    test_order_index.cpp
    test_order_book_side.cpp
    test_order_book.cpp
    test_matching_engine.cpp
    test_spsc_queue.cpp
    test_engine_service.cpp
    test_fuzzing.cpp
    test_market_data.cpp
    test_order_amend.cpp
    test_symbol_router.cpp
)

target_link_libraries(orderbook_tests PRIVATE
    orderbook_core
    Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(orderbook_tests)
