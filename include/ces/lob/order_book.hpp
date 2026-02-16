#pragma once
/**
 * @file order_book.hpp
 * @brief Cache-aware limit order book with price-time priority matching
 * 
 * Uses flat vectors for price levels (no std::map).
 * Orders stored in ObjectPool with intrusive linked lists per level.
 * Protected by mutex for optional concurrent read access.
 */

#include <ces/common/types.hpp>
#include <ces/common/macros.hpp>
#include <ces/common/concepts.hpp>
#include <ces/memory/object_pool.hpp>
#include <ces/lob/order.hpp>
#include <ces/lob/price_level.hpp>

#include <vector>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <functional>
#include <optional>

namespace ces {

/**
 * @brief Cache-aware limit order book with price-time priority
 * 
 * Key Design Decisions:
 * - Uses std::vector<PriceLevel> instead of std::map for cache efficiency
 * - Bids sorted descending, asks sorted ascending
 * - Orders stored in ObjectPool with indices, not pointers
 * - order_id -> pool_index lookup via unordered_map (reserved big, low load factor)
 * - Mutex protects all mutations (allows optional concurrent reads)
 * 
 * Thread Safety:
 * - All mutating operations are protected by mutex
 * - Default single-writer mode: matching engine is only writer
 * - Mutex enables future extension for concurrent market data snapshots
 */
class OrderBook {
public:
    /// Callback for trade notifications
    using TradeCallback = std::function<void(const Trade&)>;

private:
    // Order storage
    ObjectPool<Order> order_pool_;
    
    // Order lookup: order_id -> pool_index
    std::unordered_map<std::uint64_t, std::uint32_t> order_map_;
    
    // Price levels (sorted vectors)
    std::vector<PriceLevel> bids_;  // Descending by price
    std::vector<PriceLevel> asks_;  // Ascending by price
    
    // Trade callback
    TradeCallback trade_callback_;
    
    // Mutex for thread safety
    mutable std::mutex mutex_;
    
    // Statistics
    std::uint64_t total_trades_{0};
    std::uint64_t total_volume_{0};

public:
    /**
     * @brief Construct order book with reserved capacity
     * @param max_orders Maximum orders in pool
     * @param max_levels Maximum price levels per side
     * @param load_factor Hash map load factor (lower = faster, more memory)
     */
    explicit OrderBook(
        std::uint32_t max_orders = static_cast<std::uint32_t>(constants::DEFAULT_MAX_ORDERS),
        std::size_t max_levels = constants::DEFAULT_MAX_PRICE_LEVELS,
        float load_factor = 0.5f
    )
        : order_pool_(max_orders) {
        
        // Reserve capacity to avoid reallocations
        bids_.reserve(max_levels);
        asks_.reserve(max_levels);
        
        // Configure hash map for performance
        order_map_.reserve(max_orders);
        order_map_.max_load_factor(load_factor);
    }
    
    ~OrderBook() = default;
    
    // Non-copyable, non-movable (owns mutex)
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    
    /**
     * @brief Set callback for trade notifications
     */
    void set_trade_callback(TradeCallback callback) {
        std::lock_guard lock(mutex_);
        trade_callback_ = std::move(callback);
    }
    
    // ========================================================================
    // Order Operations
    // ========================================================================
    
    /**
     * @brief Add a new limit order
     * @param order_id Unique order ID
     * @param trader_id Trader ID
     * @param side Buy or Sell
     * @param price Limit price
     * @param qty Quantity
     * @return Order response with fill information
     */
    OrderResponse add_limit(
        OrderId order_id,
        TraderId trader_id,
        Side side,
        Price price,
        Qty qty
    );
    
    /**
     * @brief Add a market order (matches immediately, no resting)
     * @param order_id Unique order ID
     * @param trader_id Trader ID
     * @param side Buy or Sell
     * @param qty Quantity
     * @return Order response with fill information
     */
    OrderResponse add_market(
        OrderId order_id,
        TraderId trader_id,
        Side side,
        Qty qty
    );
    
    /**
     * @brief Cancel an existing order
     * @param order_id Order ID to cancel
     * @return Order response
     */
    OrderResponse cancel(OrderId order_id);
    
    /**
     * @brief Modify an existing order (cancel + new)
     * @param order_id Existing order ID
     * @param new_qty New quantity
     * @param new_price New price (if different, treated as cancel+new)
     * @return Order response
     */
    OrderResponse modify(OrderId order_id, Qty new_qty, Price new_price);
    
    // ========================================================================
    // Query Operations
    // ========================================================================
    
    /**
     * @brief Get best bid price
     * @return Best bid price, or nullopt if no bids
     */
    [[nodiscard]] std::optional<Price> best_bid() const;
    
    /**
     * @brief Get best ask price
     * @return Best ask price, or nullopt if no asks
     */
    [[nodiscard]] std::optional<Price> best_ask() const;
    
    /**
     * @brief Get mid price
     * @return Mid price, or nullopt if either side is empty
     */
    [[nodiscard]] std::optional<double> mid_price() const;
    
    /**
     * @brief Get spread in ticks
     */
    [[nodiscard]] std::optional<std::int64_t> spread() const;
    
    /**
     * @brief Get total quantity at best bid
     */
    [[nodiscard]] Qty best_bid_qty() const;
    
    /**
     * @brief Get total quantity at best ask
     */
    [[nodiscard]] Qty best_ask_qty() const;
    
    /**
     * @brief Get number of active orders
     */
    [[nodiscard]] std::size_t order_count() const;
    
    /**
     * @brief Get number of bid levels
     */
    [[nodiscard]] std::size_t bid_levels() const;
    
    /**
     * @brief Get number of ask levels
     */
    [[nodiscard]] std::size_t ask_levels() const;
    
    /**
     * @brief Get total trade count
     */
    [[nodiscard]] std::uint64_t trade_count() const;
    
    /**
     * @brief Get total traded volume
     */
    [[nodiscard]] std::uint64_t total_volume() const;
    
    /**
     * @brief Check if order exists
     */
    [[nodiscard]] bool has_order(OrderId order_id) const;
    
    /**
     * @brief Clear all orders
     */
    void clear();

private:
    // ========================================================================
    // Internal Methods (must hold mutex)
    // ========================================================================
    
    /**
     * @brief Internal add_limit without locking (caller must hold mutex_)
     */
    OrderResponse add_limit_internal(
        OrderId order_id,
        TraderId trader_id,
        Side side,
        Price price,
        Qty qty
    );
    
    /**
     * @brief Match incoming order against opposite side
     * @param taker_order_id Taker order ID
     * @param taker_trader_id Taker trader ID
     * @param side Taker side
     * @param price Limit price (or 0 for market)
     * @param qty Quantity to fill
     * @param is_market True if market order
     * @return Remaining quantity after matching
     */
    Qty match_order(
        OrderId taker_order_id,
        TraderId taker_trader_id,
        Side side,
        Price price,
        Qty qty,
        bool is_market,
        std::size_t& trade_count
    );
    
    /**
     * @brief Find or create price level
     * @param levels Vector of levels (bids or asks)
     * @param price Price to find/create
     * @param is_bid True if bid side
     * @return Iterator to the level
     */
    std::vector<PriceLevel>::iterator find_or_create_level(
        std::vector<PriceLevel>& levels,
        Price price,
        bool is_bid
    );
    
    /**
     * @brief Find price level
     */
    std::vector<PriceLevel>::iterator find_level(
        std::vector<PriceLevel>& levels,
        Price price,
        bool is_bid
    );
    
    /**
     * @brief Remove empty price level
     */
    void remove_level_if_empty(
        std::vector<PriceLevel>& levels,
        std::vector<PriceLevel>::iterator it
    );
    
    /**
     * @brief Remove order from book (internal)
     */
    void remove_order_internal(std::uint32_t pool_idx);
    
    /**
     * @brief Emit trade callback
     */
    void emit_trade(const Trade& trade);
};

} // namespace ces
