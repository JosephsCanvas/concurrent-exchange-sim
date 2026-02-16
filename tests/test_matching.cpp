/**
 * @file test_matching.cpp
 * @brief Integration tests for the matching engine
 */

#include <gtest/gtest.h>

#include <ces/engine/matching_engine.hpp>
#include <ces/engine/accounts.hpp>
#include <ces/concurrency/spsc_semaphore_queue.hpp>
#include <ces/lob/order.hpp>
#include <ces/common/types.hpp>

#include <thread>
#include <chrono>

using namespace ces;

constexpr std::size_t TEST_QUEUE_CAPACITY = 1024;

class MatchingEngineTest : public ::testing::Test {
protected:
    using Queue = SpscSemaphoreQueue<OrderEvent, TEST_QUEUE_CAPACITY>;
    
    Queue queue;
    std::unique_ptr<MatchingEngine<TEST_QUEUE_CAPACITY>> engine;
    
    void SetUp() override {
        EngineConfig config;
        config.max_orders = 10000;
        config.max_traders = 100;
        config.initial_balance = 1'000'000'000;
        engine = std::make_unique<MatchingEngine<TEST_QUEUE_CAPACITY>>(queue, config);
    }
    
    void process_event(const OrderEvent& event) {
        engine->process_event(event);
    }
};

// ============================================================================
// Direct Event Processing Tests
// ============================================================================

TEST_F(MatchingEngineTest, ProcessNewLimit) {
    auto event = OrderEvent::new_limit(
        OrderId{1}, TraderId{0}, Side::Buy, Price{100}, Qty{10}
    );
    
    process_event(event);
    
    EXPECT_EQ(engine->book().order_count(), 1);
    EXPECT_EQ(engine->book().best_bid()->get(), 100);
}

TEST_F(MatchingEngineTest, ProcessMatching) {
    // Add resting order
    process_event(OrderEvent::new_limit(
        OrderId{1}, TraderId{0}, Side::Sell, Price{100}, Qty{10}
    ));
    
    // Cross the spread
    process_event(OrderEvent::new_limit(
        OrderId{2}, TraderId{1}, Side::Buy, Price{100}, Qty{10}
    ));
    
    EXPECT_EQ(engine->book().order_count(), 0);
    EXPECT_EQ(engine->stats().trade_count.load(), 1);
    EXPECT_EQ(engine->stats().volume.load(), 10);
}

TEST_F(MatchingEngineTest, ProcessCancel) {
    process_event(OrderEvent::new_limit(
        OrderId{1}, TraderId{0}, Side::Buy, Price{100}, Qty{10}
    ));
    
    process_event(OrderEvent::cancel(OrderId{1}));
    
    EXPECT_EQ(engine->book().order_count(), 0);
}

TEST_F(MatchingEngineTest, ProcessMarket) {
    // Add resting limit
    process_event(OrderEvent::new_limit(
        OrderId{1}, TraderId{0}, Side::Sell, Price{100}, Qty{10}
    ));
    
    // Market order
    process_event(OrderEvent::new_market(
        OrderId{2}, TraderId{1}, Side::Buy, Qty{5}
    ));
    
    EXPECT_EQ(engine->stats().trade_count.load(), 1);
    EXPECT_EQ(engine->book().best_ask_qty().get(), 5);  // Remaining
}

// ============================================================================
// Account Integration Tests
// ============================================================================

TEST_F(MatchingEngineTest, AccountsUpdatedOnTrade) {
    // Two traders
    process_event(OrderEvent::new_limit(
        OrderId{1}, TraderId{0}, Side::Sell, Price{100}, Qty{10}
    ));
    
    process_event(OrderEvent::new_limit(
        OrderId{2}, TraderId{1}, Side::Buy, Price{100}, Qty{10}
    ));
    
    // Check positions
    // Trader 0 sold 10 @ 100: position = -10, balance += 1000
    // Trader 1 bought 10 @ 100: position = +10, balance -= 1000
    EXPECT_EQ(engine->accounts().get_position(TraderId{0}), -10);
    EXPECT_EQ(engine->accounts().get_position(TraderId{1}), 10);
}

TEST_F(MatchingEngineTest, AccountsCreatedAutomatically) {
    process_event(OrderEvent::new_limit(
        OrderId{1}, TraderId{42}, Side::Buy, Price{100}, Qty{10}
    ));
    
    EXPECT_NE(engine->accounts().get(TraderId{42}), nullptr);
}

// ============================================================================
// Threaded Engine Tests
// ============================================================================

TEST_F(MatchingEngineTest, ThreadedProcessing) {
    constexpr std::size_t NUM_ORDERS = 1000;
    
    // Start engine thread
    std::jthread engine_thread([this](std::stop_token st) {
        engine->run(st);
    });
    
    // Give engine time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Push orders
    for (std::size_t i = 0; i < NUM_ORDERS; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        Price price = (side == Side::Buy) ? Price{99} : Price{101};
        
        auto event = OrderEvent::new_limit(
            OrderId{i + 1}, TraderId{static_cast<std::uint32_t>(i % 10)},
            side, price, Qty{10}
        );
        queue.push(event);
    }
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Stop engine
    engine_thread.request_stop();
    engine_thread.join();
    
    EXPECT_EQ(engine->events_processed(), NUM_ORDERS);
}

TEST_F(MatchingEngineTest, ThreadedWithMatching) {
    // Start engine
    std::jthread engine_thread([this](std::stop_token st) {
        engine->run(st);
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Push crossing orders
    for (std::size_t i = 0; i < 100; ++i) {
        // Ask at 100
        queue.push(OrderEvent::new_limit(
            OrderId{i * 2 + 1}, TraderId{0}, Side::Sell, Price{100}, Qty{10}
        ));
        
        // Bid at 100 (crosses)
        queue.push(OrderEvent::new_limit(
            OrderId{i * 2 + 2}, TraderId{1}, Side::Buy, Price{100}, Qty{10}
        ));
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    engine_thread.request_stop();
    engine_thread.join();
    
    EXPECT_EQ(engine->stats().trade_count.load(), 100);
    EXPECT_EQ(engine->book().order_count(), 0);  // All matched
}

// ============================================================================
// Latency Tracking Tests
// ============================================================================

TEST_F(MatchingEngineTest, LatencyRecorded) {
    process_event(OrderEvent::new_limit(
        OrderId{1}, TraderId{0}, Side::Buy, Price{100}, Qty{10}
    ));
    
    auto stats = engine->stats().get_latency_stats();
    EXPECT_GE(stats.count, 1);
}

// ============================================================================
// Stress Test
// ============================================================================

TEST_F(MatchingEngineTest, StressTest) {
    constexpr std::size_t NUM_ORDERS = 10000;
    
    std::jthread engine_thread([this](std::stop_token st) {
        engine->run(st);
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Generate mixed workload
    for (std::size_t i = 0; i < NUM_ORDERS; ++i) {
        OrderEvent event;
        
        if (i % 10 == 0 && i > 0) {
            // Cancel every 10th order
            event = OrderEvent::cancel(OrderId{i - 5});
        } else {
            Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
            Price base_price{10000};
            Price offset{static_cast<std::int64_t>((i % 20) - 10)};
            Price price = base_price + offset;
            
            event = OrderEvent::new_limit(
                OrderId{i + 1},
                TraderId{static_cast<std::uint32_t>(i % 10)},
                side, price, Qty{10 + static_cast<std::int64_t>(i % 100)}
            );
        }
        
        queue.push(event);
    }
    
    // Wait for completion
    while (engine->events_processed() < NUM_ORDERS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    engine_thread.request_stop();
    engine_thread.join();
    
    EXPECT_EQ(engine->events_processed(), NUM_ORDERS);
    
    // Print performance info
    double orders_per_sec = NUM_ORDERS * 1'000'000.0 / elapsed_us;
    std::cout << "Stress test: " << NUM_ORDERS << " orders in " 
              << elapsed_us << " µs (" << orders_per_sec << " orders/sec)\n";
    
    auto latency = engine->stats().get_latency_stats();
    std::cout << "P99 latency: " << (latency.p99_ns / 1000.0) << " µs\n";
}
