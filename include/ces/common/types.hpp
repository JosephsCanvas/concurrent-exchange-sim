#pragma once
/**
 * @file types.hpp
 * @brief Core type definitions for the Concurrent Exchange Simulator
 * 
 * Defines Price, Qty, OrderId, TraderId with strong typing to prevent
 * accidental mixing of incompatible numeric types.
 */

// Prevent Windows min/max macros from conflicting with std::numeric_limits
#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <cstdint>
#include <limits>
#include <compare>

namespace ces {

// ============================================================================
// Strong Type Wrapper Template
// ============================================================================

/**
 * @brief Strong type wrapper to prevent accidental mixing of similar types
 * @tparam T Underlying type
 * @tparam Tag Phantom type for differentiation
 */
template<typename T, typename Tag>
struct StrongType {
    T value{};
    
    constexpr StrongType() noexcept = default;
    constexpr explicit StrongType(T v) noexcept : value(v) {}
    
    [[nodiscard]] constexpr T get() const noexcept { return value; }
    [[nodiscard]] constexpr explicit operator T() const noexcept { return value; }
    
    constexpr auto operator<=>(const StrongType&) const noexcept = default;
    constexpr bool operator==(const StrongType&) const noexcept = default;
    
    // Arithmetic operations
    constexpr StrongType operator+(StrongType rhs) const noexcept {
        return StrongType{value + rhs.value};
    }
    
    constexpr StrongType operator-(StrongType rhs) const noexcept {
        return StrongType{value - rhs.value};
    }
    
    constexpr StrongType& operator+=(StrongType rhs) noexcept {
        value += rhs.value;
        return *this;
    }
    
    constexpr StrongType& operator-=(StrongType rhs) noexcept {
        value -= rhs.value;
        return *this;
    }
};

// ============================================================================
// Type Tags
// ============================================================================

struct PriceTag {};
struct QtyTag {};
struct OrderIdTag {};
struct TraderIdTag {};
struct PoolIndexTag {};

// ============================================================================
// Core Types
// ============================================================================

/// Price in integer ticks (e.g., cents or basis points)
using Price = StrongType<std::int64_t, PriceTag>;

/// Quantity in units (shares, contracts, etc.)
using Qty = StrongType<std::int64_t, QtyTag>;

/// Unique order identifier
using OrderId = StrongType<std::uint64_t, OrderIdTag>;

/// Trader/account identifier
using TraderId = StrongType<std::uint32_t, TraderIdTag>;

/// Index into the order pool
using PoolIndex = StrongType<std::uint32_t, PoolIndexTag>;

// ============================================================================
// Constants
// ============================================================================

namespace constants {

/// Invalid pool index sentinel value
inline constexpr PoolIndex INVALID_POOL_INDEX{std::numeric_limits<std::uint32_t>::max()};

/// Invalid order ID sentinel value
inline constexpr OrderId INVALID_ORDER_ID{std::numeric_limits<std::uint64_t>::max()};

/// Invalid trader ID sentinel value
inline constexpr TraderId INVALID_TRADER_ID{std::numeric_limits<std::uint32_t>::max()};

/// Default price tick size
inline constexpr std::int64_t DEFAULT_TICK_SIZE = 1;

/// Default maximum price levels per side
inline constexpr std::size_t DEFAULT_MAX_PRICE_LEVELS = 1024;

/// Default maximum orders in pool
inline constexpr std::size_t DEFAULT_MAX_ORDERS = 1'000'000;

/// Default ring buffer capacity
inline constexpr std::size_t DEFAULT_RING_BUFFER_CAPACITY = 65536;

} // namespace constants

// ============================================================================
// Side Enumeration
// ============================================================================

enum class Side : std::uint8_t {
    Buy = 0,
    Sell = 1
};

[[nodiscard]] constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

[[nodiscard]] constexpr const char* to_string(Side s) noexcept {
    return s == Side::Buy ? "Buy" : "Sell";
}

// ============================================================================
// Order Type Enumeration
// ============================================================================

enum class OrderType : std::uint8_t {
    NewLimit = 0,
    NewMarket = 1,
    Cancel = 2,
    Modify = 3
};

[[nodiscard]] constexpr const char* to_string(OrderType t) noexcept {
    switch (t) {
        case OrderType::NewLimit:  return "NewLimit";
        case OrderType::NewMarket: return "NewMarket";
        case OrderType::Cancel:    return "Cancel";
        case OrderType::Modify:    return "Modify";
    }
    return "Unknown";
}

// ============================================================================
// Compile-time String to Enum Helper
// ============================================================================

namespace detail {

constexpr bool str_equal(const char* a, const char* b) noexcept {
    while (*a && *b) {
        if (*a++ != *b++) return false;
    }
    return *a == *b;
}

} // namespace detail

/// Parse Side from string at compile time
[[nodiscard]] constexpr Side parse_side(const char* s) noexcept {
    if (detail::str_equal(s, "Buy") || detail::str_equal(s, "B")) {
        return Side::Buy;
    }
    return Side::Sell; // Default to Sell
}

/// Parse OrderType from string at compile time
[[nodiscard]] constexpr OrderType parse_order_type(const char* s) noexcept {
    if (detail::str_equal(s, "NewLimit") || detail::str_equal(s, "L")) {
        return OrderType::NewLimit;
    }
    if (detail::str_equal(s, "NewMarket") || detail::str_equal(s, "M")) {
        return OrderType::NewMarket;
    }
    if (detail::str_equal(s, "Cancel") || detail::str_equal(s, "C")) {
        return OrderType::Cancel;
    }
    if (detail::str_equal(s, "Modify") || detail::str_equal(s, "X")) {
        return OrderType::Modify;
    }
    return OrderType::NewLimit; // Default
}

// ============================================================================
// Result/Status Types
// ============================================================================

enum class OrderResult : std::uint8_t {
    Accepted = 0,
    PartiallyFilled = 1,
    FullyFilled = 2,
    Cancelled = 3,
    Modified = 4,
    Rejected = 5,
    NotFound = 6
};

[[nodiscard]] constexpr const char* to_string(OrderResult r) noexcept {
    switch (r) {
        case OrderResult::Accepted:        return "Accepted";
        case OrderResult::PartiallyFilled: return "PartiallyFilled";
        case OrderResult::FullyFilled:     return "FullyFilled";
        case OrderResult::Cancelled:       return "Cancelled";
        case OrderResult::Modified:        return "Modified";
        case OrderResult::Rejected:        return "Rejected";
        case OrderResult::NotFound:        return "NotFound";
    }
    return "Unknown";
}

} // namespace ces

// ============================================================================
// Hash Specializations for std::unordered_map
// ============================================================================

namespace std {

template<typename T, typename Tag>
struct hash<ces::StrongType<T, Tag>> {
    std::size_t operator()(const ces::StrongType<T, Tag>& st) const noexcept {
        return std::hash<T>{}(st.get());
    }
};

} // namespace std
