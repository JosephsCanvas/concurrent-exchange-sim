#pragma once
/**
 * @file stats.hpp
 * @brief Engine statistics with atomic counters
 * 
 * Uses std::atomic_ref for thread-safe updates from matching thread
 * while allowing reads from other threads.
 */

#include <ces/common/types.hpp>
#include <ces/common/time.hpp>
#include <ces/common/macros.hpp>
#include <ces/metrics/latency.hpp>

#include <atomic>
#include <cstdint>

namespace ces {

/**
 * @brief Engine statistics container
 * 
 * All counters are atomic for thread-safe updates.
 * Uses cache-line alignment to avoid false sharing.
 */
struct EngineStats {
    // Trade statistics
    CES_CACHE_ALIGNED std::atomic<std::uint64_t> trade_count{0};
    CES_CACHE_ALIGNED std::atomic<std::uint64_t> volume{0};
    
    // Order statistics
    CES_CACHE_ALIGNED std::atomic<std::uint64_t> orders_received{0};
    CES_CACHE_ALIGNED std::atomic<std::uint64_t> orders_accepted{0};
    CES_CACHE_ALIGNED std::atomic<std::uint64_t> orders_cancelled{0};
    CES_CACHE_ALIGNED std::atomic<std::uint64_t> orders_modified{0};
    CES_CACHE_ALIGNED std::atomic<std::uint64_t> rejected_count{0};
    CES_CACHE_ALIGNED std::atomic<std::uint64_t> filled_qty{0};
    
    // Latency tracking
    LatencyHistogram latency_histogram{100'000};
    
    EngineStats() = default;
    
    // Non-copyable due to atomics
    EngineStats(const EngineStats&) = delete;
    EngineStats& operator=(const EngineStats&) = delete;
    
    // Movable
    EngineStats(EngineStats&&) = default;
    EngineStats& operator=(EngineStats&&) = default;
    
    /**
     * @brief Record a latency sample
     */
    void record_latency(Duration latency_ns) {
        latency_histogram.record(latency_ns);
    }
    
    /**
     * @brief Get latency statistics
     */
    [[nodiscard]] LatencyStats get_latency_stats() const {
        return latency_histogram.compute_stats();
    }
    
    /**
     * @brief Reset all statistics
     */
    void reset() {
        trade_count.store(0, std::memory_order_relaxed);
        volume.store(0, std::memory_order_relaxed);
        orders_received.store(0, std::memory_order_relaxed);
        orders_accepted.store(0, std::memory_order_relaxed);
        orders_cancelled.store(0, std::memory_order_relaxed);
        orders_modified.store(0, std::memory_order_relaxed);
        rejected_count.store(0, std::memory_order_relaxed);
        filled_qty.store(0, std::memory_order_relaxed);
        latency_histogram.clear();
    }
    
    /**
     * @brief Print summary to stdout
     */
    void print_summary() const;
};

/**
 * @brief Snapshot of engine stats at a point in time
 * 
 * Non-atomic copy for reporting.
 */
struct StatsSnapshot {
    std::uint64_t trade_count{0};
    std::uint64_t volume{0};
    std::uint64_t orders_received{0};
    std::uint64_t orders_accepted{0};
    std::uint64_t orders_cancelled{0};
    std::uint64_t orders_modified{0};
    std::uint64_t rejected_count{0};
    std::uint64_t filled_qty{0};
    LatencyStats latency;
    Timestamp timestamp{0};
    
    /**
     * @brief Capture snapshot from EngineStats
     */
    static StatsSnapshot capture(const EngineStats& stats) {
        StatsSnapshot snap;
        snap.trade_count = stats.trade_count.load(std::memory_order_relaxed);
        snap.volume = stats.volume.load(std::memory_order_relaxed);
        snap.orders_received = stats.orders_received.load(std::memory_order_relaxed);
        snap.orders_accepted = stats.orders_accepted.load(std::memory_order_relaxed);
        snap.orders_cancelled = stats.orders_cancelled.load(std::memory_order_relaxed);
        snap.orders_modified = stats.orders_modified.load(std::memory_order_relaxed);
        snap.rejected_count = stats.rejected_count.load(std::memory_order_relaxed);
        snap.filled_qty = stats.filled_qty.load(std::memory_order_relaxed);
        snap.latency = stats.latency_histogram.compute_stats();
        snap.timestamp = now_ns();
        return snap;
    }
};

} // namespace ces
