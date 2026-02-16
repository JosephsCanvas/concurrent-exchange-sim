#pragma once
/**
 * @file time.hpp
 * @brief High-resolution timing utilities for latency measurement
 */

#include <chrono>
#include <cstdint>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace ces {

// ============================================================================
// High-Resolution Clock Utilities
// ============================================================================

/// Nanosecond timestamp type
using Timestamp = std::uint64_t;

/// Duration in nanoseconds
using Duration = std::int64_t;

/**
 * @brief Get current timestamp in nanoseconds since epoch
 * @return High-resolution timestamp
 */
[[nodiscard]] inline Timestamp now_ns() noexcept {
    using Clock = std::chrono::high_resolution_clock;
    auto now = Clock::now();
    return static_cast<Timestamp>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()
        ).count()
    );
}

/**
 * @brief Get current timestamp in microseconds since epoch
 * @return Timestamp in microseconds
 */
[[nodiscard]] inline Timestamp now_us() noexcept {
    using Clock = std::chrono::high_resolution_clock;
    auto now = Clock::now();
    return static_cast<Timestamp>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()
        ).count()
    );
}

/**
 * @brief Calculate elapsed time in nanoseconds
 * @param start Start timestamp
 * @return Elapsed nanoseconds
 */
[[nodiscard]] inline Duration elapsed_ns(Timestamp start) noexcept {
    return static_cast<Duration>(now_ns() - start);
}

/**
 * @brief Convert nanoseconds to microseconds
 */
[[nodiscard]] constexpr double ns_to_us(Duration ns) noexcept {
    return static_cast<double>(ns) / 1000.0;
}

/**
 * @brief Convert nanoseconds to milliseconds
 */
[[nodiscard]] constexpr double ns_to_ms(Duration ns) noexcept {
    return static_cast<double>(ns) / 1'000'000.0;
}

// ============================================================================
// RDTSC (Read Time-Stamp Counter) - for ultra-low overhead timing
// ============================================================================

#if defined(__x86_64__) || defined(_M_X64)

/**
 * @brief Read CPU timestamp counter (x86_64 only)
 * @note Not synchronized across cores, use for relative measurements only
 */
[[nodiscard]] inline std::uint64_t rdtsc() noexcept {
#ifdef _MSC_VER
    return __rdtsc();
#else
    std::uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<std::uint64_t>(hi) << 32) | lo;
#endif
}

/**
 * @brief Read CPU timestamp counter with serialization
 * @note More accurate but higher overhead
 */
[[nodiscard]] inline std::uint64_t rdtscp() noexcept {
#ifdef _MSC_VER
    unsigned int aux;
    return __rdtscp(&aux);
#else
    std::uint32_t lo, hi, aux;
    __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
    return (static_cast<std::uint64_t>(hi) << 32) | lo;
#endif
}

#else

// Fallback for non-x86 platforms
[[nodiscard]] inline std::uint64_t rdtsc() noexcept {
    return now_ns();
}

[[nodiscard]] inline std::uint64_t rdtscp() noexcept {
    return now_ns();
}

#endif

} // namespace ces
