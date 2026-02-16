/**
 * @file test_order_book.cpp
 * @brief Unit tests for the limit order book
 */

#include <gtest/gtest.h>

#include <ces/lob/order_book.hpp>
#include <ces/lob/order.hpp>
#include <ces/common/types.hpp>

#include <vector>

using namespace ces;

class OrderBookTest : public ::testing::Test {
protected:
    OrderBook book{10000, 100};
    std::vector<Trade> trades;
    
    void SetUp() override {
        trades.clear();
        book.set_trade_callback([this](const Trade& trade) {
            trades.push_back(trade);
        });
    }
};

// ============================================================================
// Basic Operations
// ============================================================================

TEST_F(OrderBookTest, EmptyBook) {
    EXPECT_EQ(book.order_count(), 0);
    EXPECT_EQ(book.bid_levels(), 0);
    EXPECT_EQ(book.ask_levels(), 0);
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_FALSE(book.spread().has_value());
}

TEST_F(OrderBookTest, AddSingleBid) {
    auto response = book.add_limit(
        OrderId{1}, TraderId{0}, Side::Buy, Price{100}, Qty{10}
    );
    
    EXPECT_EQ(response.result, OrderResult::Accepted);
    EXPECT_EQ(response.qty_filled.get(), 0);
    EXPECT_EQ(response.qty_remaining.get(), 10);
    
    EXPECT_EQ(book.order_count(), 1);
    EXPECT_EQ(book.bid_levels(), 1);
    EXPECT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(book.best_bid()->get(), 100);
    EXPECT_EQ(book.best_bid_qty().get(), 10);
}

TEST_F(OrderBookTest, AddSingleAsk) {
    auto response = book.add_limit(
        OrderId{1}, TraderId{0}, Side::Sell, Price{100}, Qty{10}
    );
    
    EXPECT_EQ(response.result, OrderResult::Accepted);
    EXPECT_EQ(book.order_count(), 1);
    EXPECT_EQ(book.ask_levels(), 1);
    EXPECT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(book.best_ask()->get(), 100);
}

TEST_F(OrderBookTest, DuplicateOrderIdRejected) {
    book.add_limit(OrderId{1}, TraderId{0}, Side::Buy, Price{100}, Qty{10});
    
    auto response = book.add_limit(
        OrderId{1}, TraderId{0}, Side::Buy, Price{101}, Qty{20}
    );
    
    EXPECT_EQ(response.result, OrderResult::Rejected);
    EXPECT_EQ(book.order_count(), 1);
}

// ============================================================================
// Price Level Management
// ============================================================================

TEST_F(OrderBookTest, MultipleBidLevels) {
    book.add_limit(OrderId{1}, TraderId{0}, Side::Buy, Price{100}, Qty{10});
    book.add_limit(OrderId{2}, TraderId{0}, Side::Buy, Price{99}, Qty{20});
    book.add_limit(OrderId{3}, TraderId{0}, Side::Buy, Price{101}, Qty{30});
    
    EXPECT_EQ(book.bid_levels(), 3);
    EXPECT_EQ(book.best_bid()->get(), 101);  // Highest bid is best
    EXPECT_EQ(book.best_bid_qty().get(), 30);
}

TEST_F(OrderBookTest, MultipleAskLevels) {
    book.add_limit(OrderId{1}, TraderId{0}, Side::Sell, Price{100}, Qty{10});
    book.add_limit(OrderId{2}, TraderId{0}, Side::Sell, Price{101}, Qty{20});
    book.add_limit(OrderId{3}, TraderId{0}, Side::Sell, Price{99}, Qty{30});
    
    EXPECT_EQ(book.ask_levels(), 3);
    EXPECT_EQ(book.best_ask()->get(), 99);  // Lowest ask is best
    EXPECT_EQ(book.best_ask_qty().get(), 30);
}

TEST_F(OrderBookTest, OrdersAtSamePrice) {
    book.add_limit(OrderId{1}, TraderId{0}, Side::Buy, Price{100}, Qty{10});
    book.add_limit(OrderId{2}, TraderId{1}, Side::Buy, Price{100}, Qty{20});
    book.add_limit(OrderId{3}, TraderId{2}, Side::Buy, Price{100}, Qty{30});
    
    EXPECT_EQ(book.bid_levels(), 1);
    EXPECT_EQ(book.order_count(), 3);
    EXPECT_EQ(book.best_bid_qty().get(), 60);  // Total qty at level
}

// ============================================================================
// Matching
// ============================================================================

TEST_F(OrderBookTest, FullMatch) {
    // Add resting ask
    book.add_limit(OrderId{1}, TraderId{0}, Side::Sell, Price{100}, Qty{10});
    
    // Cross with equal buy
    auto response = book.add_limit(
        OrderId{2}, TraderId{1}, Side::Buy, Price{100}, Qty{10}
    );
    
    EXPECT_EQ(response.result, OrderResult::FullyFilled);
    EXPECT_EQ(response.qty_filled.get(), 10);
    EXPECT_EQ(response.trade_count, 1);
    
    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].qty.get(), 10);
    EXPECT_EQ(trades[0].price.get(), 100);
    EXPECT_EQ(trades[0].maker_order_id.get(), 1);
    EXPECT_EQ(trades[0].taker_order_id.get(), 2);
    
    EXPECT_EQ(book.order_count(), 0);
}

TEST_F(OrderBookTest, PartialMatch) {
    // Add resting ask
    book.add_limit(OrderId{1}, TraderId{0}, Side::Sell, Price{100}, Qty{10});
    
    // Cross with larger buy
    auto response = book.add_limit(
        OrderId{2}, TraderId{1}, Side::Buy, Price{100}, Qty{15}
    );
    
    EXPECT_EQ(response.result, OrderResult::PartiallyFilled);
    EXPECT_EQ(response.qty_filled.get(), 10);
    EXPECT_EQ(response.qty_remaining.get(), 5);
    
    EXPECT_EQ(book.order_count(), 1);  // Remaining buy rests
    EXPECT_EQ(book.best_bid()->get(), 100);
    EXPECT_EQ(book.best_bid_qty().get(), 5);
}

TEST_F(OrderBookTest, MultiLevelMatch) {
    // Build ask side
    book.add_limit(OrderId{1}, TraderId{0}, Side::Sell, Price{100}, Qty{10});
    book.add_limit(OrderId{2}, TraderId{0}, Side::Sell, Price{101}, Qty{10});
    book.add_limit(OrderId{3}, TraderId{0}, Side::Sell, Price{102}, Qty{10});
    
    // Large buy sweeps multiple levels
    auto response = book.add_limit(
        OrderId{4}, TraderId{1}, Side::Buy, Price{102}, Qty{25}
    );
    
    // 25 filled (10+10+5), all qty consumed = FullyFilled
    EXPECT_EQ(response.result, OrderResult::FullyFilled);
    EXPECT_EQ(response.qty_filled.get(), 25);
    EXPECT_EQ(trades.size(), 3);  // One trade per level
    
    EXPECT_EQ(book.ask_levels(), 1);  // Only partially filled level remains
    EXPECT_EQ(book.best_ask()->get(), 102);
    EXPECT_EQ(book.best_ask_qty().get(), 5);
}

TEST_F(OrderBookTest, PriceTimePriority) {
    // Add two orders at same price
    book.add_limit(OrderId{1}, TraderId{0}, Side::Sell, Price{100}, Qty{10});
    book.add_limit(OrderId{2}, TraderId{1}, Side::Sell, Price{100}, Qty{10});
    
    // Match should take first order (time priority)
    auto response = book.add_limit(
        OrderId{3}, TraderId{2}, Side::Buy, Price{100}, Qty{10}
    );
    
    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].maker_order_id.get(), 1);  // First order matched
    
    EXPECT_TRUE(book.has_order(OrderId{2}));  // Second still in book
    EXPECT_FALSE(book.has_order(OrderId{1}));
}

TEST_F(OrderBookTest, NoMatchWhenPricesDoNotCross) {
    book.add_limit(OrderId{1}, TraderId{0}, Side::Sell, Price{100}, Qty{10});
    
    auto response = book.add_limit(
        OrderId{2}, TraderId{1}, Side::Buy, Price{99}, Qty{10}
    );
    
    EXPECT_EQ(response.result, OrderResult::Accepted);
    EXPECT_EQ(trades.size(), 0);
    EXPECT_EQ(book.order_count(), 2);
}

// ============================================================================
// Market Orders
// ============================================================================

TEST_F(OrderBookTest, MarketOrderFull) {
    book.add_limit(OrderId{1}, TraderId{0}, Side::Sell, Price{100}, Qty{10});
    book.add_limit(OrderId{2}, TraderId{0}, Side::Sell, Price{101}, Qty{10});
    
    auto response = book.add_market(
        OrderId{3}, TraderId{1}, Side::Buy, Qty{15}
    );
    
    EXPECT_EQ(response.result, OrderResult::FullyFilled);
    EXPECT_EQ(response.qty_filled.get(), 15);
    EXPECT_EQ(trades.size(), 2);
}

TEST_F(OrderBookTest, MarketOrderPartial) {
    book.add_limit(OrderId{1}, TraderId{0}, Side::Sell, Price{100}, Qty{10});
    
    auto response = book.add_market(
        OrderId{2}, TraderId{1}, Side::Buy, Qty{20}
    );
    
    EXPECT_EQ(response.result, OrderResult::PartiallyFilled);
    EXPECT_EQ(response.qty_filled.get(), 10);
    EXPECT_EQ(response.qty_remaining.get(), 10);
    EXPECT_EQ(book.order_count(), 0);  // Market orders don't rest
}

// ============================================================================
// Cancel
// ============================================================================

TEST_F(OrderBookTest, CancelOrder) {
    book.add_limit(OrderId{1}, TraderId{0}, Side::Buy, Price{100}, Qty{10});
    
    auto response = book.cancel(OrderId{1});
    
    EXPECT_EQ(response.result, OrderResult::Cancelled);
    EXPECT_EQ(book.order_count(), 0);
    EXPECT_FALSE(book.has_order(OrderId{1}));
}

TEST_F(OrderBookTest, CancelNonexistent) {
    auto response = book.cancel(OrderId{999});
    
    EXPECT_EQ(response.result, OrderResult::NotFound);
}

TEST_F(OrderBookTest, CancelDoesNotMatch) {
    book.add_limit(OrderId{1}, TraderId{0}, Side::Sell, Price{100}, Qty{10});
    book.cancel(OrderId{1});
    
    // This buy would have matched if order wasn't cancelled
    auto response = book.add_limit(
        OrderId{2}, TraderId{1}, Side::Buy, Price{100}, Qty{10}
    );
    
    EXPECT_EQ(response.result, OrderResult::Accepted);
    EXPECT_EQ(trades.size(), 0);
}

// ============================================================================
// Modify
// ============================================================================

TEST_F(OrderBookTest, ModifyQuantityDown) {
    book.add_limit(OrderId{1}, TraderId{0}, Side::Buy, Price{100}, Qty{10});
    
    auto response = book.modify(OrderId{1}, Qty{5}, Price{0});
    
    EXPECT_EQ(response.result, OrderResult::Modified);
    EXPECT_EQ(book.best_bid_qty().get(), 5);
}

TEST_F(OrderBookTest, ModifyPrice) {
    book.add_limit(OrderId{1}, TraderId{0}, Side::Buy, Price{100}, Qty{10});
    
    auto response = book.modify(OrderId{1}, Qty{10}, Price{101});
    
    EXPECT_TRUE(response.success());
    EXPECT_EQ(book.best_bid()->get(), 101);
}

TEST_F(OrderBookTest, ModifyNonexistent) {
    auto response = book.modify(OrderId{999}, Qty{10}, Price{100});
    
    EXPECT_EQ(response.result, OrderResult::NotFound);
}

// ============================================================================
// Book Queries
// ============================================================================

TEST_F(OrderBookTest, Spread) {
    book.add_limit(OrderId{1}, TraderId{0}, Side::Buy, Price{99}, Qty{10});
    book.add_limit(OrderId{2}, TraderId{0}, Side::Sell, Price{101}, Qty{10});
    
    auto spread = book.spread();
    EXPECT_TRUE(spread.has_value());
    EXPECT_EQ(*spread, 2);  // 101 - 99
}

TEST_F(OrderBookTest, MidPrice) {
    book.add_limit(OrderId{1}, TraderId{0}, Side::Buy, Price{99}, Qty{10});
    book.add_limit(OrderId{2}, TraderId{0}, Side::Sell, Price{101}, Qty{10});
    
    auto mid = book.mid_price();
    EXPECT_TRUE(mid.has_value());
    EXPECT_DOUBLE_EQ(*mid, 100.0);
}

TEST_F(OrderBookTest, Clear) {
    book.add_limit(OrderId{1}, TraderId{0}, Side::Buy, Price{100}, Qty{10});
    book.add_limit(OrderId{2}, TraderId{0}, Side::Sell, Price{101}, Qty{10});
    
    book.clear();
    
    EXPECT_EQ(book.order_count(), 0);
    EXPECT_EQ(book.bid_levels(), 0);
    EXPECT_EQ(book.ask_levels(), 0);
}
