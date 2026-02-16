/**
 * @file order_book.cpp
 * @brief Implementation of cache-aware limit order book
 */

#include <ces/lob/order_book.hpp>
#include <ces/common/macros.hpp>

#include <algorithm>

namespace ces {

// ============================================================================
// Order Operations
// ============================================================================

OrderResponse OrderBook::add_limit_internal(
    OrderId order_id,
    TraderId trader_id,
    Side side,
    Price price,
    Qty qty
) {
    // Internal version without lock - caller must hold mutex_
    OrderResponse response;
    response.order_id = order_id;
    
    // Check for duplicate order ID
    if CES_UNLIKELY(order_map_.contains(order_id.get())) {
        response.result = OrderResult::Rejected;
        return response;
    }
    
    // Try to match first
    std::size_t trades = 0;
    Qty remaining = match_order(order_id, trader_id, side, price, qty, false, trades);
    response.trade_count = trades;
    response.qty_filled = qty - remaining;
    response.qty_remaining = remaining;
    
    if (remaining.get() <= 0) {
        // Fully filled
        response.result = OrderResult::FullyFilled;
        return response;
    }
    
    // Rest order in book
    std::uint32_t pool_idx = order_pool_.allocate(
        order_id, trader_id, side, price, remaining
    );
    
    if CES_UNLIKELY(pool_idx == ObjectPool<Order>::INVALID_INDEX) {
        response.result = OrderResult::Rejected;
        return response;
    }
    
    // Add to lookup map
    order_map_[order_id.get()] = pool_idx;
    
    // Add to appropriate price level
    auto& levels = (side == Side::Buy) ? bids_ : asks_;
    auto it = find_or_create_level(levels, price, side == Side::Buy);
    it->push_back(order_pool_, pool_idx);
    
    response.result = (trades > 0) ? OrderResult::PartiallyFilled : OrderResult::Accepted;
    return response;
}

OrderResponse OrderBook::add_limit(
    OrderId order_id,
    TraderId trader_id,
    Side side,
    Price price,
    Qty qty
) {
    std::lock_guard lock(mutex_);
    return add_limit_internal(order_id, trader_id, side, price, qty);
}

OrderResponse OrderBook::add_market(
    OrderId order_id,
    TraderId trader_id,
    Side side,
    Qty qty
) {
    std::lock_guard lock(mutex_);
    
    OrderResponse response;
    response.order_id = order_id;
    
    // Market orders never rest - match immediately
    std::size_t trades = 0;
    Qty remaining = match_order(order_id, trader_id, side, Price{0}, qty, true, trades);
    
    response.trade_count = trades;
    response.qty_filled = qty - remaining;
    response.qty_remaining = remaining;
    response.result = (remaining.get() <= 0) 
        ? OrderResult::FullyFilled 
        : OrderResult::PartiallyFilled;
    
    return response;
}

OrderResponse OrderBook::cancel(OrderId order_id) {
    std::lock_guard lock(mutex_);
    
    OrderResponse response;
    response.order_id = order_id;
    
    // Find order
    auto it = order_map_.find(order_id.get());
    if CES_UNLIKELY(it == order_map_.end()) {
        response.result = OrderResult::NotFound;
        return response;
    }
    
    std::uint32_t pool_idx = it->second;
    const Order& order = order_pool_[pool_idx];
    response.qty_remaining = order.qty_remaining;
    
    // Remove from book
    remove_order_internal(pool_idx);
    order_map_.erase(it);
    
    response.result = OrderResult::Cancelled;
    return response;
}

OrderResponse OrderBook::modify(OrderId order_id, Qty new_qty, Price new_price) {
    std::lock_guard lock(mutex_);
    
    OrderResponse response;
    response.order_id = order_id;
    
    // Find existing order
    auto it = order_map_.find(order_id.get());
    if CES_UNLIKELY(it == order_map_.end()) {
        response.result = OrderResult::NotFound;
        return response;
    }
    
    std::uint32_t pool_idx = it->second;
    Order& order = order_pool_[pool_idx];
    
    // If price changed, treat as cancel + new (loses priority)
    if (new_price.get() != 0 && new_price != order.price) {
        TraderId trader_id = order.trader_id;
        Side side = order.side;
        
        // Cancel existing (remove_order_internal handles deallocation)
        remove_order_internal(pool_idx);
        order_map_.erase(it);
        
        // Add new (reuse same order_id for simplicity) - use internal to avoid deadlock
        return add_limit_internal(order_id, trader_id, side, new_price, new_qty);
    }
    
    // Same price - just update quantity (keep priority if reducing)
    if (new_qty < order.qty_remaining) {
        // Reduce quantity - keep position
        auto& levels = (order.side == Side::Buy) ? bids_ : asks_;
        auto level_it = find_level(levels, order.price, order.side == Side::Buy);
        
        if (level_it != levels.end()) {
            Qty diff = order.qty_remaining - new_qty;
            level_it->reduce_qty(diff);
        }
        
        order.qty_remaining = new_qty;
        response.qty_remaining = new_qty;
        response.result = OrderResult::Modified;
    } else {
        // Increase quantity - loses priority (cancel + new)
        TraderId trader_id = order.trader_id;
        Side side = order.side;
        Price price = order.price;
        
        // remove_order_internal handles deallocation
        remove_order_internal(pool_idx);
        order_map_.erase(it);
        
        // Use internal to avoid deadlock
        return add_limit_internal(order_id, trader_id, side, price, new_qty);
    }
    
    return response;
}

// ============================================================================
// Query Operations
// ============================================================================

std::optional<Price> OrderBook::best_bid() const {
    std::lock_guard lock(mutex_);
    
    for (const auto& level : bids_) {
        if (!level.empty()) {
            return level.price;
        }
    }
    return std::nullopt;
}

std::optional<Price> OrderBook::best_ask() const {
    std::lock_guard lock(mutex_);
    
    for (const auto& level : asks_) {
        if (!level.empty()) {
            return level.price;
        }
    }
    return std::nullopt;
}

std::optional<double> OrderBook::mid_price() const {
    auto bid = best_bid();
    auto ask = best_ask();
    
    if (bid && ask) {
        return (static_cast<double>(bid->get()) + static_cast<double>(ask->get())) / 2.0;
    }
    return std::nullopt;
}

std::optional<std::int64_t> OrderBook::spread() const {
    auto bid = best_bid();
    auto ask = best_ask();
    
    if (bid && ask) {
        return ask->get() - bid->get();
    }
    return std::nullopt;
}

Qty OrderBook::best_bid_qty() const {
    std::lock_guard lock(mutex_);
    
    for (const auto& level : bids_) {
        if (!level.empty()) {
            return level.total_qty;
        }
    }
    return Qty{0};
}

Qty OrderBook::best_ask_qty() const {
    std::lock_guard lock(mutex_);
    
    for (const auto& level : asks_) {
        if (!level.empty()) {
            return level.total_qty;
        }
    }
    return Qty{0};
}

std::size_t OrderBook::order_count() const {
    std::lock_guard lock(mutex_);
    return order_pool_.size();
}

std::size_t OrderBook::bid_levels() const {
    std::lock_guard lock(mutex_);
    
    std::size_t count = 0;
    for (const auto& level : bids_) {
        if (!level.empty()) ++count;
    }
    return count;
}

std::size_t OrderBook::ask_levels() const {
    std::lock_guard lock(mutex_);
    
    std::size_t count = 0;
    for (const auto& level : asks_) {
        if (!level.empty()) ++count;
    }
    return count;
}

std::uint64_t OrderBook::trade_count() const {
    std::lock_guard lock(mutex_);
    return total_trades_;
}

std::uint64_t OrderBook::total_volume() const {
    std::lock_guard lock(mutex_);
    return total_volume_;
}

bool OrderBook::has_order(OrderId order_id) const {
    std::lock_guard lock(mutex_);
    return order_map_.contains(order_id.get());
}

void OrderBook::clear() {
    std::lock_guard lock(mutex_);
    
    order_pool_.clear();
    order_map_.clear();
    bids_.clear();
    asks_.clear();
    total_trades_ = 0;
    total_volume_ = 0;
}

// ============================================================================
// Internal Methods
// ============================================================================

Qty OrderBook::match_order(
    OrderId taker_order_id,
    TraderId taker_trader_id,
    Side side,
    Price price,
    Qty qty,
    bool is_market,
    std::size_t& trade_count
) {
    // Opposite side
    auto& levels = (side == Side::Buy) ? asks_ : bids_;
    
    Qty remaining = qty;
    trade_count = 0;
    
    for (auto level_it = levels.begin(); 
         level_it != levels.end() && remaining.get() > 0; ) {
        
        // Price check for limit orders
        if (!is_market) {
            if (side == Side::Buy && level_it->price > price) {
                break;  // No more matchable levels
            }
            if (side == Side::Sell && level_it->price < price) {
                break;
            }
        }
        
        // Match against orders at this level
        while (remaining.get() > 0 && !level_it->empty()) {
            std::uint32_t maker_idx = level_it->front_idx();
            Order& maker = order_pool_[maker_idx];
            
            Qty fill_qty{std::min(remaining.get(), maker.qty_remaining.get())};
            
            // Create trade
            Trade trade(
                maker.order_id, taker_order_id,
                maker.trader_id, taker_trader_id,
                maker.price, fill_qty, side
            );
            
            // Update maker
            maker.qty_remaining -= fill_qty;
            level_it->reduce_qty(fill_qty);
            remaining -= fill_qty;
            
            // Emit trade
            emit_trade(trade);
            ++trade_count;
            ++total_trades_;
            total_volume_ += fill_qty.get();
            
            // Remove maker if filled
            if (maker.qty_remaining.get() <= 0) {
                std::uint32_t idx_to_remove = maker_idx;
                level_it->remove(order_pool_, idx_to_remove);
                order_map_.erase(maker.order_id.get());
                order_pool_.deallocate(idx_to_remove);
            }
        }
        
        // Remove empty level or advance
        if (level_it->empty()) {
            level_it = levels.erase(level_it);
        } else {
            ++level_it;
        }
    }
    
    return remaining;
}

std::vector<PriceLevel>::iterator OrderBook::find_or_create_level(
    std::vector<PriceLevel>& levels,
    Price price,
    bool is_bid
) {
    // Binary search for insertion point
    auto it = std::lower_bound(levels.begin(), levels.end(), price,
        [is_bid](const PriceLevel& level, Price p) {
            return is_bid ? (level.price > p) : (level.price < p);
        }
    );
    
    // Check if already exists
    if (it != levels.end() && it->price == price) {
        return it;
    }
    
    // Insert new level
    return levels.insert(it, PriceLevel{price});
}

std::vector<PriceLevel>::iterator OrderBook::find_level(
    std::vector<PriceLevel>& levels,
    Price price,
    bool is_bid
) {
    auto it = std::lower_bound(levels.begin(), levels.end(), price,
        [is_bid](const PriceLevel& level, Price p) {
            return is_bid ? (level.price > p) : (level.price < p);
        }
    );
    
    if (it != levels.end() && it->price == price) {
        return it;
    }
    return levels.end();
}

void OrderBook::remove_level_if_empty(
    std::vector<PriceLevel>& levels,
    std::vector<PriceLevel>::iterator it
) {
    if (it != levels.end() && it->empty()) {
        levels.erase(it);
    }
}

void OrderBook::remove_order_internal(std::uint32_t pool_idx) {
    CES_ASSERT(order_pool_.is_valid(pool_idx));
    
    Order& order = order_pool_[pool_idx];
    auto& levels = (order.side == Side::Buy) ? bids_ : asks_;
    
    auto it = find_level(levels, order.price, order.side == Side::Buy);
    if (it != levels.end()) {
        it->remove(order_pool_, pool_idx);
        remove_level_if_empty(levels, it);
    }
    
    order_pool_.deallocate(pool_idx);
}

void OrderBook::emit_trade(const Trade& trade) {
    if (trade_callback_) {
        trade_callback_(trade);
    }
}

} // namespace ces
