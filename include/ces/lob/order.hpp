#pragma once
/**
 * @file order.hpp
 * @brief Order structure and order events for the exchange simulator
 * 
 * Orders use pool indices for intrusive linked list (not pointers)
 * to maintain cache efficiency and avoid pointer chasing.
 */

#include <ces/common/types.hpp>
#include <ces/common/time.hpp>
#include <ces/common/macros.hpp>

#include <limits>

namespace ces {

/// Sentinel value for invalid pool indices (matches ObjectPool::INVALID_INDEX)
constexpr std::uint32_t INVALID_POOL_INDEX = std::numeric_limits<std::uint32_t>::max();

/**
 * @brief Order stored in the object pool
 * 
 * Uses intrusive linked list via indices for O(1) insertion/removal
 * within a price level's FIFO queue.
 */
struct Order {
    OrderId order_id{constants::INVALID_ORDER_ID};
    TraderId trader_id{constants::INVALID_TRADER_ID};
    Side side{Side::Buy};
    Price price{0};
    Qty qty_remaining{0};
    Qty qty_original{0};
    Timestamp timestamp{0};
    
    // Intrusive linked list (indices into OrderPool)
    std::uint32_t next_idx{INVALID_POOL_INDEX};
    std::uint32_t prev_idx{INVALID_POOL_INDEX};
    
    Order() = default;
    
    Order(OrderId id, TraderId trader, Side s, Price p, Qty qty, Timestamp ts = 0)
        : order_id(id)
        , trader_id(trader)
        , side(s)
        , price(p)
        , qty_remaining(qty)
        , qty_original(qty)
        , timestamp(ts == 0 ? now_ns() : ts)
        , next_idx(INVALID_POOL_INDEX)
        , prev_idx(INVALID_POOL_INDEX) {
    }
    
    [[nodiscard]] bool is_valid() const noexcept {
        return order_id != constants::INVALID_ORDER_ID;
    }
    
    [[nodiscard]] bool is_filled() const noexcept {
        return qty_remaining.get() <= 0;
    }
    
    [[nodiscard]] Qty qty_filled() const noexcept {
        return qty_original - qty_remaining;
    }
};

/**
 * @brief Order event submitted to the matching engine queue
 * 
 * POD-like structure for efficient queue transfer.
 * Uses aggregate of event-specific data rather than union for simplicity.
 */
struct OrderEvent {
    OrderType type{OrderType::NewLimit};
    OrderId order_id{constants::INVALID_ORDER_ID};
    TraderId trader_id{constants::INVALID_TRADER_ID};
    Side side{Side::Buy};
    Price price{0};
    Qty qty{0};
    Timestamp enqueue_time{0};  // For latency measurement
    
    // Factory methods for clarity
    
    [[nodiscard]] static OrderEvent new_limit(
        OrderId id, TraderId trader, Side side, Price price, Qty qty
    ) noexcept {
        return OrderEvent{
            .type = OrderType::NewLimit,
            .order_id = id,
            .trader_id = trader,
            .side = side,
            .price = price,
            .qty = qty,
            .enqueue_time = now_ns()
        };
    }
    
    [[nodiscard]] static OrderEvent new_market(
        OrderId id, TraderId trader, Side side, Qty qty
    ) noexcept {
        return OrderEvent{
            .type = OrderType::NewMarket,
            .order_id = id,
            .trader_id = trader,
            .side = side,
            .price = Price{0},
            .qty = qty,
            .enqueue_time = now_ns()
        };
    }
    
    [[nodiscard]] static OrderEvent cancel(OrderId id) noexcept {
        return OrderEvent{
            .type = OrderType::Cancel,
            .order_id = id,
            .trader_id = constants::INVALID_TRADER_ID,
            .side = Side::Buy,
            .price = Price{0},
            .qty = Qty{0},
            .enqueue_time = now_ns()
        };
    }
    
    [[nodiscard]] static OrderEvent modify(
        OrderId id, Qty new_qty, Price new_price = Price{0}
    ) noexcept {
        return OrderEvent{
            .type = OrderType::Modify,
            .order_id = id,
            .trader_id = constants::INVALID_TRADER_ID,
            .side = Side::Buy,
            .price = new_price,
            .qty = new_qty,
            .enqueue_time = now_ns()
        };
    }
};

/**
 * @brief Trade execution report
 */
struct Trade {
    OrderId maker_order_id{constants::INVALID_ORDER_ID};
    OrderId taker_order_id{constants::INVALID_ORDER_ID};
    TraderId maker_trader_id{constants::INVALID_TRADER_ID};
    TraderId taker_trader_id{constants::INVALID_TRADER_ID};
    Price price{0};
    Qty qty{0};
    Side taker_side{Side::Buy};
    Timestamp timestamp{0};
    
    Trade() = default;
    
    Trade(OrderId maker_oid, OrderId taker_oid, 
          TraderId maker_tid, TraderId taker_tid,
          Price p, Qty q, Side taker_s)
        : maker_order_id(maker_oid)
        , taker_order_id(taker_oid)
        , maker_trader_id(maker_tid)
        , taker_trader_id(taker_tid)
        , price(p)
        , qty(q)
        , taker_side(taker_s)
        , timestamp(now_ns()) {
    }
};

/**
 * @brief Result of an order operation
 */
struct OrderResponse {
    OrderResult result{OrderResult::Rejected};
    OrderId order_id{constants::INVALID_ORDER_ID};
    Qty qty_filled{0};
    Qty qty_remaining{0};
    std::size_t trade_count{0};  // Number of trades generated
    
    [[nodiscard]] bool success() const noexcept {
        return result != OrderResult::Rejected && 
               result != OrderResult::NotFound;
    }
};

} // namespace ces
