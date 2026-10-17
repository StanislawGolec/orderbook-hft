#include "orderbook/cpu_affinity.hpp"

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace orderbook {

bool pin_current_thread_to_core(uint32_t core_id) noexcept {
    if (core_id >= 64) {
        return false;
    }
    DWORD_PTR mask = static_cast<DWORD_PTR>(1ULL << core_id);
    DWORD_PTR prev = SetThreadAffinityMask(GetCurrentThread(), mask);
    return prev != 0;
}

uint32_t get_hardware_concurrency() noexcept {
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    return static_cast<uint32_t>(sys_info.dwNumberOfProcessors);
}

bool set_thread_high_priority() noexcept {
    return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST) != 0;
}

}  // namespace orderbook

#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>

#include <thread>

namespace orderbook {

bool pin_current_thread_to_core(uint32_t core_id) noexcept {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
}

uint32_t get_hardware_concurrency() noexcept {
    return std::thread::hardware_concurrency();
}

bool set_thread_high_priority() noexcept {
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    return pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0;
}

}  // namespace orderbook

#else
#include <thread>

namespace orderbook {

bool pin_current_thread_to_core(uint32_t core_id) noexcept {
    (void)core_id;
    return false;
}

uint32_t get_hardware_concurrency() noexcept {
    return std::thread::hardware_concurrency();
}

bool set_thread_high_priority() noexcept {
    return false;
}

}  // namespace orderbook

#endif
