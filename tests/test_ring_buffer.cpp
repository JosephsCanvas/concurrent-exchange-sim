/**
 * @file test_ring_buffer.cpp
 * @brief Unit tests for ring buffer and SPSC semaphore queue
 */

#include <gtest/gtest.h>

#include <ces/concurrency/ring_buffer.hpp>
#include <ces/concurrency/spsc_semaphore_queue.hpp>

#include <thread>
#include <vector>
#include <atomic>

using namespace ces;

// ============================================================================
// RingBuffer Tests
// ============================================================================

TEST(RingBufferTest, BasicPushPop) {
    RingBuffer<int, 16> buffer;
    
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_EQ(buffer.capacity(), 16);
    
    EXPECT_TRUE(buffer.push(42));
    EXPECT_FALSE(buffer.empty());
    EXPECT_EQ(buffer.size(), 1);
    
    auto value = buffer.pop();
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, 42);
    EXPECT_TRUE(buffer.empty());
}

TEST(RingBufferTest, FillAndEmpty) {
    RingBuffer<int, 8> buffer;
    
    // Fill the buffer
    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(buffer.push(i));
    }
    
    EXPECT_TRUE(buffer.full());
    EXPECT_FALSE(buffer.push(100));  // Should fail - full
    
    // Empty the buffer
    for (int i = 0; i < 8; ++i) {
        auto value = buffer.pop();
        EXPECT_TRUE(value.has_value());
        EXPECT_EQ(*value, i);
    }
    
    EXPECT_TRUE(buffer.empty());
    EXPECT_FALSE(buffer.pop().has_value());  // Should fail - empty
}

TEST(RingBufferTest, Wraparound) {
    RingBuffer<int, 4> buffer;
    
    // Push and pop to cause wraparound
    for (int round = 0; round < 10; ++round) {
        for (int i = 0; i < 4; ++i) {
            EXPECT_TRUE(buffer.push(round * 4 + i));
        }
        for (int i = 0; i < 4; ++i) {
            auto value = buffer.pop();
            EXPECT_TRUE(value.has_value());
            EXPECT_EQ(*value, round * 4 + i);
        }
    }
}

TEST(RingBufferTest, Peek) {
    RingBuffer<int, 8> buffer;
    
    EXPECT_EQ(buffer.peek(), nullptr);
    
    EXPECT_TRUE(buffer.push(42));
    EXPECT_TRUE(buffer.push(43));
    
    EXPECT_NE(buffer.peek(), nullptr);
    EXPECT_EQ(*buffer.peek(), 42);
    EXPECT_EQ(buffer.size(), 2);  // Peek doesn't remove
    
    EXPECT_TRUE(buffer.pop().has_value());
    EXPECT_EQ(*buffer.peek(), 43);
}

// ============================================================================
// SpscSemaphoreQueue Tests
// ============================================================================

TEST(SpscQueueTest, BasicOperations) {
    SpscSemaphoreQueue<int, 16> queue;
    
    // Non-blocking operations
    int value = 0;
    EXPECT_FALSE(queue.try_pop(value));  // Empty
    
    EXPECT_TRUE(queue.try_push(42));
    EXPECT_TRUE(queue.try_pop(value));
    EXPECT_EQ(value, 42);
}

TEST(SpscQueueTest, BlockingPushPop) {
    SpscSemaphoreQueue<int, 8> queue;
    
    // Push values
    queue.push(1);
    queue.push(2);
    queue.push(3);
    
    // Pop values
    EXPECT_EQ(queue.pop(), 1);
    EXPECT_EQ(queue.pop(), 2);
    EXPECT_EQ(queue.pop(), 3);
}

TEST(SpscQueueTest, ConcurrentProducerConsumer) {
    constexpr std::size_t NUM_ITEMS = 10000;
    SpscSemaphoreQueue<std::uint64_t, 256> queue;
    
    std::atomic<std::uint64_t> sum_produced{0};
    std::atomic<std::uint64_t> sum_consumed{0};
    std::atomic<bool> producer_done{false};
    
    // Producer thread
    std::thread producer([&]() {
        for (std::uint64_t i = 1; i <= NUM_ITEMS; ++i) {
            queue.push(i);
            sum_produced.fetch_add(i, std::memory_order_relaxed);
        }
        producer_done.store(true, std::memory_order_release);
    });
    
    // Consumer thread
    std::thread consumer([&]() {
        std::uint64_t count = 0;
        while (count < NUM_ITEMS) {
            std::uint64_t value;
            if (queue.try_pop(value)) {
                sum_consumed.fetch_add(value, std::memory_order_relaxed);
                ++count;
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_EQ(sum_produced.load(), sum_consumed.load());
    
    // Expected sum: 1 + 2 + ... + NUM_ITEMS = NUM_ITEMS * (NUM_ITEMS + 1) / 2
    std::uint64_t expected_sum = NUM_ITEMS * (NUM_ITEMS + 1) / 2;
    EXPECT_EQ(sum_consumed.load(), expected_sum);
}

TEST(SpscQueueTest, Timeout) {
    SpscSemaphoreQueue<int, 8> queue;
    
    int value;
    auto start = std::chrono::steady_clock::now();
    bool result = queue.try_pop_for(value, std::chrono::milliseconds(50));
    auto end = std::chrono::steady_clock::now();
    
    EXPECT_FALSE(result);
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_GE(elapsed.count(), 40);  // Should have waited at least ~50ms
}

TEST(SpscQueueTest, StructType) {
    struct TestStruct {
        int a;
        double b;
        char c;
    };
    
    SpscSemaphoreQueue<TestStruct, 16> queue;
    
    TestStruct input{42, 3.14, 'x'};
    queue.push(input);
    
    TestStruct output = queue.pop();
    EXPECT_EQ(output.a, 42);
    EXPECT_DOUBLE_EQ(output.b, 3.14);
    EXPECT_EQ(output.c, 'x');
}

TEST(SpscQueueTest, SizeApprox) {
    SpscSemaphoreQueue<int, 16> queue;
    
    EXPECT_EQ(queue.size_approx(), 0);
    EXPECT_TRUE(queue.empty_approx());
    
    queue.push(1);
    queue.push(2);
    queue.push(3);
    
    EXPECT_EQ(queue.size_approx(), 3);
    EXPECT_FALSE(queue.empty_approx());
    EXPECT_FALSE(queue.full_approx());
    
    int value;
    EXPECT_TRUE(queue.try_pop(value));
    EXPECT_EQ(queue.size_approx(), 2);
}
