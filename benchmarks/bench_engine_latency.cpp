/**
 * @file bench_engine_latency.cpp
 * @brief End-to-end latency benchmarks for the matching engine
 */

#include <benchmark/benchmark.h>

#include <ces/engine/matching_engine.hpp>
#include <ces/concurrency/spsc_semaphore_queue.hpp>
#include <ces/lob/order.hpp>
#include <ces/common/types.hpp>
#include <ces/common/time.hpp>

#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>

using namespace ces;

constexpr std::size_t QUEUE_CAPACITY = 65536;

// ============================================================================
// End-to-End Latency Benchmark
// ============================================================================

static void BM_EndToEndLatency(benchmark::State& state) {
    const std::size_t num_orders = state.range(0);
    
    using Queue = SpscSemaphoreQueue<OrderEvent, QUEUE_CAPACITY>;
    Queue queue;
    
    EngineConfig config;
    config.max_orders = 100000;
    config.max_traders = 100;
    
    MatchingEngine<QUEUE_CAPACITY> engine(queue, config);
    
    // Start engine
    std::jthread engine_thread([&engine](std::stop_token st) {
        engine.run(st);
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    std::vector<Duration> latencies;
    latencies.reserve(num_orders);
    
    for (auto _ : state) {
        state.PauseTiming();
        latencies.clear();
        engine.book().clear();
        std::uint64_t order_id = 1;
        state.ResumeTiming();
        
        // Generate and measure
        for (std::size_t i = 0; i < num_orders; ++i) {
            Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
            Price price = (side == Side::Buy) ? Price{9990} : Price{10010};
            
            Timestamp start = now_ns();
            
            queue.push(OrderEvent::new_limit(
                OrderId{order_id++},
                TraderId{0},
                side,
                price,
                Qty{10}
            ));
            
            // Wait for processing
            while (engine.events_processed() < order_id - 1) {
                // Spin
            }
            
            Timestamp end = now_ns();
            latencies.push_back(static_cast<Duration>(end - start));
        }
        
        // Calculate percentiles
        std::sort(latencies.begin(), latencies.end());
        
        state.PauseTiming();
        
        if (!latencies.empty()) {
            std::size_t p50_idx = latencies.size() * 50 / 100;
            std::size_t p99_idx = latencies.size() * 99 / 100;
            
            state.counters["p50_us"] = latencies[p50_idx] / 1000.0;
            state.counters["p99_us"] = latencies[p99_idx] / 1000.0;
            state.counters["max_us"] = latencies.back() / 1000.0;
        }
        
        state.ResumeTiming();
    }
    
    engine_thread.request_stop();
    engine_thread.join();
    
    state.SetItemsProcessed(state.iterations() * num_orders);
}

BENCHMARK(BM_EndToEndLatency)->Arg(100)->Arg(1000)->Arg(10000);

// ============================================================================
// Queue Latency Benchmark (measure queue overhead)
// ============================================================================

static void BM_QueueLatency(benchmark::State& state) {
    using Queue = SpscSemaphoreQueue<OrderEvent, QUEUE_CAPACITY>;
    Queue queue;
    
    std::atomic<bool> running{true};
    std::vector<Duration> latencies;
    latencies.reserve(10000);
    
    // Consumer thread
    std::jthread consumer([&queue, &running, &latencies]() {
        OrderEvent event;
        while (running.load() || !queue.empty_approx()) {
            if (queue.try_pop(event)) {
                Duration latency = static_cast<Duration>(now_ns() - event.enqueue_time);
                latencies.push_back(latency);
            }
        }
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    for (auto _ : state) {
        auto event = OrderEvent::new_limit(
            OrderId{1}, TraderId{0}, Side::Buy, Price{100}, Qty{10}
        );
        
        queue.push(event);
    }
    
    // Wait for drain
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    running.store(false);
    consumer.join();
    
    // Report latencies
    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        std::size_t p50_idx = latencies.size() * 50 / 100;
        std::size_t p99_idx = latencies.size() * 99 / 100;
        
        state.counters["queue_p50_ns"] = static_cast<double>(latencies[p50_idx]);
        state.counters["queue_p99_ns"] = static_cast<double>(latencies[p99_idx]);
    }
    
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_QueueLatency);

// ============================================================================
// Matching Latency Benchmark (with trades)
// ============================================================================

static void BM_MatchingLatency(benchmark::State& state) {
    using Queue = SpscSemaphoreQueue<OrderEvent, QUEUE_CAPACITY>;
    Queue queue;
    
    EngineConfig config;
    config.max_orders = 100000;
    config.max_traders = 100;
    
    MatchingEngine<QUEUE_CAPACITY> engine(queue, config);
    
    std::jthread engine_thread([&engine](std::stop_token st) {
        engine.run(st);
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    std::uint64_t order_id = 1;
    
    for (auto _ : state) {
        // Add ask
        queue.push(OrderEvent::new_limit(
            OrderId{order_id++}, TraderId{0}, Side::Sell, Price{100}, Qty{10}
        ));
        
        // Wait
        while (engine.events_processed() < order_id - 1) {}
        
        // Crossing bid (triggers match)
        Timestamp start = now_ns();
        
        queue.push(OrderEvent::new_limit(
            OrderId{order_id++}, TraderId{1}, Side::Buy, Price{100}, Qty{10}
        ));
        
        while (engine.events_processed() < order_id - 1) {}
        
        Timestamp end = now_ns();
        
        benchmark::DoNotOptimize(end - start);
    }
    
    engine_thread.request_stop();
    engine_thread.join();
    
    state.SetItemsProcessed(state.iterations());
    
    // Report trade count
    state.counters["trades"] = static_cast<double>(engine.stats().trade_count.load());
    
    auto latency = engine.stats().get_latency_stats();
    state.counters["engine_p99_us"] = latency.p99_ns / 1000.0;
}

BENCHMARK(BM_MatchingLatency);

// ============================================================================
// Throughput Under Load
// ============================================================================

static void BM_ThroughputUnderLoad(benchmark::State& state) {
    const std::size_t num_producers = state.range(0);
    
    using Queue = SpscSemaphoreQueue<OrderEvent, QUEUE_CAPACITY>;
    Queue queue;
    
    EngineConfig config;
    config.max_orders = 1000000;
    
    MatchingEngine<QUEUE_CAPACITY> engine(queue, config);
    
    std::jthread engine_thread([&engine](std::stop_token st) {
        engine.run(st);
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    constexpr std::size_t ORDERS_PER_ITER = 10000;
    
    for (auto _ : state) {
        std::uint64_t start_processed = engine.events_processed();
        
        for (std::size_t i = 0; i < ORDERS_PER_ITER; ++i) {
            Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
            Price price = (side == Side::Buy) ? Price{9990} : Price{10010};
            
            queue.push(OrderEvent::new_limit(
                OrderId{start_processed + i + 1},
                TraderId{static_cast<std::uint32_t>(i % num_producers)},
                side,
                price,
                Qty{10}
            ));
        }
        
        // Wait for all to process
        while (engine.events_processed() < start_processed + ORDERS_PER_ITER) {
            std::this_thread::yield();
        }
    }
    
    engine_thread.request_stop();
    engine_thread.join();
    
    state.SetItemsProcessed(state.iterations() * ORDERS_PER_ITER);
}

BENCHMARK(BM_ThroughputUnderLoad)->Arg(1)->Arg(4)->Arg(8);

// ============================================================================
// Main
// ============================================================================

BENCHMARK_MAIN();
