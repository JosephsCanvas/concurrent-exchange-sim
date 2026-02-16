#pragma once
/**
 * @file concepts.hpp
 * @brief C++20 Concepts for type constraints in the exchange simulator
 */

#include <concepts>
#include <type_traits>

namespace ces {

// ============================================================================
// Numeric Concepts
// ============================================================================

/**
 * @brief Concept for types that support basic arithmetic operations
 */
template<typename T>
concept Numeric = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
    { a - b } -> std::convertible_to<T>;
    { a * b } -> std::convertible_to<T>;
    { a / b } -> std::convertible_to<T>;
    { a < b } -> std::convertible_to<bool>;
    { a > b } -> std::convertible_to<bool>;
    { a <= b } -> std::convertible_to<bool>;
    { a >= b } -> std::convertible_to<bool>;
    { a == b } -> std::convertible_to<bool>;
};

/**
 * @brief Concept for integral types (used for quantities, IDs)
 */
template<typename T>
concept IntegralNumeric = std::integral<T> || requires(T t) {
    { t.get() } -> std::integral;
};

/**
 * @brief Concept for price-like types
 * Must be ordered and support comparison
 */
template<typename T>
concept PriceLike = requires(T a, T b) {
    { a <=> b } -> std::convertible_to<std::strong_ordering>;
    { a == b } -> std::convertible_to<bool>;
    { a.get() } -> std::integral;
};

/**
 * @brief Concept for quantity-like types
 * Must support arithmetic and comparison
 */
template<typename T>
concept QtyLike = requires(T a, T b) {
    { a + b } -> std::same_as<T>;
    { a - b } -> std::same_as<T>;
    { a += b } -> std::same_as<T&>;
    { a -= b } -> std::same_as<T&>;
    { a > b } -> std::convertible_to<bool>;
    { a.get() } -> std::integral;
};

// ============================================================================
// Container Concepts
// ============================================================================

/**
 * @brief Concept for types that can be default constructed and trivially copied
 * Used for ring buffer elements
 */
template<typename T>
concept RingBufferElement = 
    std::is_default_constructible_v<T> &&
    std::is_trivially_copyable_v<T>;

/**
 * @brief Concept for types that can be stored in the object pool
 */
template<typename T>
concept Poolable = 
    std::is_default_constructible_v<T> &&
    std::is_move_constructible_v<T>;

// ============================================================================
// Callable Concepts
// ============================================================================

/**
 * @brief Concept for trade callback functions
 */
template<typename F, typename Trade>
concept TradeCallback = std::invocable<F, const Trade&>;

/**
 * @brief Concept for order event handlers
 */
template<typename F, typename OrderEvent>
concept OrderEventHandler = std::invocable<F, const OrderEvent&>;

// ============================================================================
// Allocator Concepts
// ============================================================================

/**
 * @brief Concept for custom allocators compatible with our memory pools
 */
template<typename A, typename T>
concept PoolAllocator = requires(A alloc) {
    { alloc.allocate() } -> std::same_as<T*>;
    { alloc.deallocate(std::declval<T*>()) } -> std::same_as<void>;
    { alloc.capacity() } -> std::convertible_to<std::size_t>;
    { alloc.size() } -> std::convertible_to<std::size_t>;
};

} // namespace ces
