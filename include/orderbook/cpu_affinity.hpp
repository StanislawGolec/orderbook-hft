#pragma once

#include <cstdint>

namespace orderbook {

// Pins calling thread to a dedicated logical CPU core
bool pin_current_thread_to_core(uint32_t core_id) noexcept;

// Returns hardware core concurrency count
uint32_t get_hardware_concurrency() noexcept;

// Sets thread to highest execution priority
bool set_thread_high_priority() noexcept;

}  // namespace orderbook
