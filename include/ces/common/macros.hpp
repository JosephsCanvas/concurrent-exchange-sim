#pragma once
/**
 * @file macros.hpp
 * @brief Common macros for performance hints and debugging
 */

#include <cassert>

namespace ces {

// ============================================================================
// Cache Line Size
// ============================================================================

/// Standard cache line size (64 bytes on most modern CPUs)
inline constexpr std::size_t CACHE_LINE_SIZE = 64;

// ============================================================================
// Branch Prediction Hints
// ============================================================================

/// Hint that condition is likely to be true (C++20)
/// Usage: if CES_LIKELY(condition) { ... }
#define CES_LIKELY(x)   (x) [[likely]]
/// Hint that condition is unlikely to be true (C++20)
/// Usage: if CES_UNLIKELY(condition) { ... }
#define CES_UNLIKELY(x) (x) [[unlikely]]

// ============================================================================
// Force Inline
// ============================================================================

#if defined(_MSC_VER)
    #define CES_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
    #define CES_FORCE_INLINE inline __attribute__((always_inline))
#else
    #define CES_FORCE_INLINE inline
#endif

// ============================================================================
// No Inline (for cold paths)
// ============================================================================

#if defined(_MSC_VER)
    #define CES_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    #define CES_NOINLINE __attribute__((noinline))
#else
    #define CES_NOINLINE
#endif

// ============================================================================
// Prefetch Hints
// ============================================================================

#if defined(__GNUC__) || defined(__clang__)
    /// Prefetch for read
    #define CES_PREFETCH_READ(addr)  __builtin_prefetch((addr), 0, 3)
    /// Prefetch for write
    #define CES_PREFETCH_WRITE(addr) __builtin_prefetch((addr), 1, 3)
#else
    #define CES_PREFETCH_READ(addr)  ((void)0)
    #define CES_PREFETCH_WRITE(addr) ((void)0)
#endif

// ============================================================================
// Debug Assertions
// ============================================================================

#ifndef NDEBUG
    /// Debug-only assertion
    #define CES_ASSERT(cond) assert(cond)
    /// Debug-only assertion with message
    #define CES_ASSERT_MSG(cond, msg) assert((cond) && (msg))
#else
    #define CES_ASSERT(cond) ((void)0)
    #define CES_ASSERT_MSG(cond, msg) ((void)0)
#endif

// ============================================================================
// Unreachable Code Hint
// ============================================================================

#if defined(__GNUC__) || defined(__clang__)
    #define CES_UNREACHABLE() __builtin_unreachable()
#elif defined(_MSC_VER)
    #define CES_UNREACHABLE() __assume(0)
#else
    #define CES_UNREACHABLE() ((void)0)
#endif

// ============================================================================
// Alignment Helpers
// ============================================================================

/// Align struct/variable to cache line boundary
#define CES_CACHE_ALIGNED alignas(ces::CACHE_LINE_SIZE)

/// Padding to fill cache line
#define CES_CACHE_PAD(name, size) \
    char name[ces::CACHE_LINE_SIZE - ((size) % ces::CACHE_LINE_SIZE)]

} // namespace ces
