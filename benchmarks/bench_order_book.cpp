/**
 * @file bench_order_book.cpp
 * @brief Benchmarks for order book operations
 */

#include <benchmark/benchmark.h>

#include <ces/lob/order_book.hpp>
#include <ces/lob/order.hpp>
#include <ces/common/types.hpp>

#include <random>

using namespace ces;

// ============================================================================
// Add Order Benchmarks
// ============================================================================

static void BM_AddOrder(benchmark::State& state) {
    OrderBook book(100000, 1000);
    std::uint64_t order_id = 1;
    std::mt19937_64 rng(12345);
    std::uniform_int_distribution<std::int64_t> price_dist(9900, 10100);
    std::uniform_int_distribution<std::int64_t> qty_dist(1, 100);
    
    for (auto _ : state) {
        Side side = (order_id % 2 == 0) ? Side::Buy : Side::Sell;
        Price price{price_dist(rng)};
        Qty qty{qty_dist(rng)};
        
        auto response = book.add_limit(
            OrderId{order_id++},
            TraderId{0},
            side,
            price,
            qty
        );
        
        benchmark::DoNotOptimize(response);
        
        // Reset periodically to avoid memory exhaustion
        if (order_id % 10000 == 0) {
            book.clear();
            order_id = 1;
        }
    }
    
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_AddOrder);

// ============================================================================
// Cancel Order Benchmarks
// ============================================================================

static void BM_CancelOrder(benchmark::State& state) {
    OrderBook book(100000, 1000);
    std::mt19937_64 rng(12345);
    std::uniform_int_distribution<std::int64_t> price_dist(9900, 10100);
    
    // Pre-populate book
    for (std::uint64_t i = 1; i <= 10000; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        book.add_limit(
            OrderId{i},
            TraderId{0},
            side,
            Price{price_dist(rng)},
            Qty{100}
        );
    }
    
    std::uint64_t cancel_id = 1;
    
    for (auto _ : state) {
        state.PauseTiming();
        
        // Re-add if we've cancelled it
        if (!book.has_order(OrderId{cancel_id})) {
            Side side = (cancel_id % 2 == 0) ? Side::Buy : Side::Sell;
            book.add_limit(
                OrderId{cancel_id},
                TraderId{0},
                side,
                Price{price_dist(rng)},
                Qty{100}
            );
        }
        
        state.ResumeTiming();
        
        auto response = book.cancel(OrderId{cancel_id});
        benchmark::DoNotOptimize(response);
        
        cancel_id = (cancel_id % 10000) + 1;
    }
    
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_CancelOrder);

// ============================================================================
// Match Hot Path Benchmark
// ============================================================================

static void BM_MatchHotPath(benchmark::State& state) {
    OrderBook book(100000, 1000);
    std::uint64_t order_id = 1;
    
    // Build initial book
    for (int i = 0; i < 100; ++i) {
        book.add_limit(OrderId{order_id++}, TraderId{0}, Side::Buy, Price{9990 - i}, Qty{100});
        book.add_limit(OrderId{order_id++}, TraderId{0}, Side::Sell, Price{10010 + i}, Qty{100});
    }
    
    for (auto _ : state) {
        // Add order that crosses the spread (triggers match)
        auto response = book.add_limit(
            OrderId{order_id++},
            TraderId{1},
            Side::Buy,
            Price{10010},
            Qty{10}
        );
        
        benchmark::DoNotOptimize(response);
        
        // Replenish the ask side
        book.add_limit(
            OrderId{order_id++},
            TraderId{0},
            Side::Sell,
            Price{10010},
            Qty{10}
        );
    }
    
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_MatchHotPath);

// ============================================================================
// Best Bid/Ask Query Benchmark
// ============================================================================

static void BM_BestBidAsk(benchmark::State& state) {
    OrderBook book(100000, 1000);
    std::mt19937_64 rng(12345);
    std::uniform_int_distribution<std::int64_t> price_dist(9900, 10100);
    
    // Populate book
    for (std::uint64_t i = 1; i <= 10000; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        book.add_limit(
            OrderId{i},
            TraderId{0},
            side,
            Price{price_dist(rng)},
            Qty{100}
        );
    }
    
    for (auto _ : state) {
        auto bid = book.best_bid();
        auto ask = book.best_ask();
        benchmark::DoNotOptimize(bid);
        benchmark::DoNotOptimize(ask);
    }
    
    state.SetItemsProcessed(state.iterations() * 2);  // Two queries per iteration
}

BENCHMARK(BM_BestBidAsk);

// ============================================================================
// Order Lookup Benchmark
// ============================================================================

static void BM_OrderLookup(benchmark::State& state) {
    OrderBook book(100000, 1000);
    std::mt19937_64 rng(12345);
    std::uniform_int_distribution<std::int64_t> price_dist(9900, 10100);
    
    // Populate book
    constexpr std::uint64_t NUM_ORDERS = 10000;
    for (std::uint64_t i = 1; i <= NUM_ORDERS; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        book.add_limit(
            OrderId{i},
            TraderId{0},
            side,
            Price{price_dist(rng)},
            Qty{100}
        );
    }
    
    std::uniform_int_distribution<std::uint64_t> id_dist(1, NUM_ORDERS);
    
    for (auto _ : state) {
        OrderId id{id_dist(rng)};
        bool exists = book.has_order(id);
        benchmark::DoNotOptimize(exists);
    }
    
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_OrderLookup);

// ============================================================================
// Throughput Benchmark (orders/second)
// ============================================================================

static void BM_Throughput(benchmark::State& state) {
    const std::size_t batch_size = state.range(0);
    OrderBook book(1000000, 10000);
    std::mt19937_64 rng(12345);
    std::uniform_int_distribution<std::int64_t> price_dist(9900, 10100);
    std::uniform_int_distribution<std::int64_t> qty_dist(1, 100);
    
    std::uint64_t order_id = 1;
    
    for (auto _ : state) {
        for (std::size_t i = 0; i < batch_size; ++i) {
            Side side = (order_id % 2 == 0) ? Side::Buy : Side::Sell;
            Price price{price_dist(rng)};
            Qty qty{qty_dist(rng)};
            
            auto response = book.add_limit(
                OrderId{order_id++},
                TraderId{0},
                side,
                price,
                qty
            );
            
            benchmark::DoNotOptimize(response);
        }
        
        state.PauseTiming();
        book.clear();
        order_id = 1;
        state.ResumeTiming();
    }
    
    state.SetItemsProcessed(state.iterations() * batch_size);
}

BENCHMARK(BM_Throughput)->Arg(1000)->Arg(10000)->Arg(100000);

// ============================================================================
// Main
// ============================================================================

BENCHMARK_MAIN();
