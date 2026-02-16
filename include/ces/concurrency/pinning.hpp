#pragma once
/**
 * @file pinning.hpp
 * @brief Thread affinity utilities for pinning threads to CPU cores
 * 
 * Uses pthread_setaffinity_np on Linux, stub on other platforms.
 */

#include <ces/common/macros.hpp>

#include <cstdint>
#include <thread>

#if defined(__linux__)
    #include <pthread.h>
    #include <sched.h>
    #define CES_HAS_THREAD_AFFINITY 1
#elif defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #define CES_HAS_THREAD_AFFINITY 1
#else
    #define CES_HAS_THREAD_AFFINITY 0
#endif

namespace ces {

/**
 * @brief Result of thread pinning operation
 */
enum class PinResult {
    Success,
    NotSupported,
    InvalidCore,
    PermissionDenied,
    Failed
};

/**
 * @brief Get the number of available CPU cores
 * @return Number of hardware threads
 */
[[nodiscard]] inline std::uint32_t get_num_cores() noexcept {
    return std::thread::hardware_concurrency();
}

/**
 * @brief Pin the current thread to a specific CPU core
 * 
 * @param core_id CPU core ID (0-based)
 * @return Result of the operation
 * 
 * On Linux: Uses pthread_setaffinity_np
 * On Windows: Uses SetThreadAffinityMask
 * Other platforms: Returns NotSupported
 */
[[nodiscard]] inline PinResult pin_thread_to_core(std::uint32_t core_id) noexcept {
#if defined(__linux__)
    if (core_id >= get_num_cores()) {
        return PinResult::InvalidCore;
    }
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    
    int result = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    
    if (result == 0) {
        return PinResult::Success;
    } else if (result == EPERM) {
        return PinResult::PermissionDenied;
    } else if (result == EINVAL) {
        return PinResult::InvalidCore;
    }
    return PinResult::Failed;

#elif defined(_WIN32)
    if (core_id >= get_num_cores()) {
        return PinResult::InvalidCore;
    }
    
    DWORD_PTR mask = 1ULL << core_id;
    DWORD_PTR result = SetThreadAffinityMask(GetCurrentThread(), mask);
    
    if (result != 0) {
        return PinResult::Success;
    }
    
    DWORD error = GetLastError();
    if (error == ERROR_ACCESS_DENIED) {
        return PinResult::PermissionDenied;
    }
    return PinResult::Failed;

#else
    (void)core_id;
    return PinResult::NotSupported;
#endif
}

/**
 * @brief Pin a std::jthread to a specific CPU core
 * 
 * @param thread Thread to pin (must be callable via native_handle)
 * @param core_id CPU core ID (0-based)
 * @return Result of the operation
 * 
 * @note Must be called from within the thread or with appropriate permissions
 */
#if defined(__linux__)
[[nodiscard]] inline PinResult pin_jthread_to_core(std::jthread& thread, std::uint32_t core_id) noexcept {
    if (core_id >= get_num_cores()) {
        return PinResult::InvalidCore;
    }
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    
    int result = pthread_setaffinity_np(thread.native_handle(), sizeof(cpu_set_t), &cpuset);
    
    if (result == 0) {
        return PinResult::Success;
    } else if (result == EPERM) {
        return PinResult::PermissionDenied;
    } else if (result == EINVAL) {
        return PinResult::InvalidCore;
    }
    return PinResult::Failed;
}
#else
[[nodiscard]] inline PinResult pin_jthread_to_core(std::jthread& /*thread*/, std::uint32_t /*core_id*/) noexcept {
    return PinResult::NotSupported;
}
#endif

/**
 * @brief Set thread priority to high (real-time on Linux)
 * @return true if successful
 */
[[nodiscard]] inline bool set_thread_high_priority() noexcept {
#if defined(__linux__)
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    
    // Try SCHED_FIFO first (requires root)
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0) {
        return true;
    }
    
    // Fall back to SCHED_RR
    param.sched_priority = sched_get_priority_max(SCHED_RR);
    return pthread_setschedparam(pthread_self(), SCHED_RR, &param) == 0;

#elif defined(_WIN32)
    return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST) != 0;

#else
    return false;
#endif
}

/**
 * @brief Convert PinResult to string for logging
 */
[[nodiscard]] constexpr const char* to_string(PinResult result) noexcept {
    switch (result) {
        case PinResult::Success:          return "Success";
        case PinResult::NotSupported:     return "NotSupported";
        case PinResult::InvalidCore:      return "InvalidCore";
        case PinResult::PermissionDenied: return "PermissionDenied";
        case PinResult::Failed:           return "Failed";
    }
    return "Unknown";
}

} // namespace ces
