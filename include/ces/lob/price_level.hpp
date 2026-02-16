#pragma once
/**
 * @file price_level.hpp
 * @brief Price level structure for the limit order book
 * 
 * Each price level maintains a FIFO queue of orders using intrusive
 * linked list indices into the order pool.
 */

#include <ces/common/types.hpp>
#include <ces/common/macros.hpp>
#include <ces/memory/object_pool.hpp>
#include <ces/lob/order.hpp>

namespace ces {

/**
 * @brief A single price level in the order book
 * 
 * Maintains orders in FIFO order using intrusive linked list.
 * Uses indices into ObjectPool rather than pointers for cache efficiency.
 */
struct PriceLevel {
    Price price{0};
    Qty total_qty{0};
    std::uint32_t order_count{0};
    
    // Head and tail of FIFO queue (indices into OrderPool)
    std::uint32_t head_idx{INVALID_POOL_INDEX};
    std::uint32_t tail_idx{INVALID_POOL_INDEX};
    
    PriceLevel() = default;
    explicit PriceLevel(Price p) : price(p) {}
    
    [[nodiscard]] bool empty() const noexcept {
        return order_count == 0;
    }
    
    /**
     * @brief Add order to back of queue
     * @param pool Order pool containing the order
     * @param order_idx Index of order in pool
     */
    void push_back(ObjectPool<Order>& pool, std::uint32_t order_idx) noexcept {
        CES_ASSERT(pool.is_valid(order_idx));
        Order& order = pool[order_idx];
        
        order.prev_idx = tail_idx;
        order.next_idx = INVALID_POOL_INDEX;
        
        if (tail_idx != INVALID_POOL_INDEX) {
            pool[tail_idx].next_idx = order_idx;
        } else {
            // Queue was empty
            head_idx = order_idx;
        }
        
        tail_idx = order_idx;
        total_qty += order.qty_remaining;
        ++order_count;
    }
    
    /**
     * @brief Remove order from queue
     * @param pool Order pool containing the order
     * @param order_idx Index of order in pool
     */
    void remove(ObjectPool<Order>& pool, std::uint32_t order_idx) noexcept {
        CES_ASSERT(pool.is_valid(order_idx));
        Order& order = pool[order_idx];
        
        // Update prev link
        if (order.prev_idx != INVALID_POOL_INDEX) {
            pool[order.prev_idx].next_idx = order.next_idx;
        } else {
            // Was head
            head_idx = order.next_idx;
        }
        
        // Update next link
        if (order.next_idx != INVALID_POOL_INDEX) {
            pool[order.next_idx].prev_idx = order.prev_idx;
        } else {
            // Was tail
            tail_idx = order.prev_idx;
        }
        
        total_qty -= order.qty_remaining;
        --order_count;
        
        // Clear order's links
        order.prev_idx = INVALID_POOL_INDEX;
        order.next_idx = INVALID_POOL_INDEX;
    }
    
    /**
     * @brief Get front order (for matching)
     * @param pool Order pool
     * @return Pointer to front order, or nullptr if empty
     */
    [[nodiscard]] Order* front(ObjectPool<Order>& pool) noexcept {
        if (head_idx == INVALID_POOL_INDEX) {
            return nullptr;
        }
        return &pool[head_idx];
    }
    
    [[nodiscard]] const Order* front(const ObjectPool<Order>& pool) const noexcept {
        if (head_idx == INVALID_POOL_INDEX) {
            return nullptr;
        }
        return &pool[head_idx];
    }
    
    /**
     * @brief Get front order index
     */
    [[nodiscard]] std::uint32_t front_idx() const noexcept {
        return head_idx;
    }
    
    /**
     * @brief Update total quantity after partial fill
     * @param filled_qty Quantity that was filled
     */
    void reduce_qty(Qty filled_qty) noexcept {
        CES_ASSERT(total_qty >= filled_qty);
        total_qty -= filled_qty;
    }
};

} // namespace ces
