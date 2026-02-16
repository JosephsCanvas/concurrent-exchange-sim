#pragma once
/**
 * @file latency.hpp
 * @brief Latency measurement and percentile calculation utilities
 */

// Prevent Windows min/max macros from conflicting with std::numeric_limits
#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <ces/common/types.hpp>
#include <ces/common/time.hpp>
#include <ces/common/macros.hpp>

#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>

namespace ces {

/**
 * @brief Latency statistics summary
 */
struct LatencyStats {
    double mean_ns{0.0};
    double median_ns{0.0};
    double p50_ns{0.0};
    double p90_ns{0.0};
    double p95_ns{0.0};
    double p99_ns{0.0};
    double p999_ns{0.0};
    Duration min_ns{std::numeric_limits<Duration>::max()};
    Duration max_ns{0};
    std::size_t count{0};
    
    /**
     * @brief Print stats to stdout
     */
    void print() const;
};

/**
 * @brief Latency histogram with preallocated buckets
 * 
 * Uses a ring buffer for samples to avoid unbounded growth.
 * Calculates percentiles on demand from stored samples.
 */
class LatencyHistogram {
public:
    static constexpr std::size_t DEFAULT_SAMPLE_SIZE = 100'000;

private:
    std::vector<Duration> samples_;
    std::size_t capacity_;
    std::size_t write_pos_{0};
    std::size_t count_{0};
    
    Duration min_{std::numeric_limits<Duration>::max()};
    Duration max_{0};
    Duration sum_{0};
    
    mutable std::mutex mutex_;

public:
    /**
     * @brief Construct histogram with sample capacity
     * @param capacity Maximum samples to store (older samples overwritten)
     */
    explicit LatencyHistogram(std::size_t capacity = DEFAULT_SAMPLE_SIZE)
        : capacity_(capacity) {
        samples_.resize(capacity);
    }
    
    /**
     * @brief Record a latency sample
     * @param latency_ns Latency in nanoseconds
     */
    void record(Duration latency_ns) noexcept {
        std::lock_guard lock(mutex_);
        
        samples_[write_pos_] = latency_ns;
        write_pos_ = (write_pos_ + 1) % capacity_;
        ++count_;
        
        min_ = std::min(min_, latency_ns);
        max_ = std::max(max_, latency_ns);
        sum_ += latency_ns;
    }
    
    /**
     * @brief Calculate statistics from recorded samples
     * @return Latency statistics
     */
    [[nodiscard]] LatencyStats compute_stats() const {
        std::lock_guard lock(mutex_);
        
        if (count_ == 0) {
            return {};
        }
        
        LatencyStats stats;
        stats.count = count_;
        stats.min_ns = min_;
        stats.max_ns = max_;
        
        // Get actual samples
        std::size_t sample_count = std::min(count_, capacity_);
        std::vector<Duration> sorted(samples_.begin(), samples_.begin() + sample_count);
        std::sort(sorted.begin(), sorted.end());
        
        // Mean
        stats.mean_ns = static_cast<double>(sum_) / static_cast<double>(count_);
        
        // Percentiles
        auto percentile = [&sorted, sample_count](double p) -> double {
            if (sample_count == 0) return 0.0;
            double index = (p / 100.0) * (sample_count - 1);
            std::size_t lower = static_cast<std::size_t>(std::floor(index));
            std::size_t upper = static_cast<std::size_t>(std::ceil(index));
            if (lower == upper) {
                return static_cast<double>(sorted[lower]);
            }
            double frac = index - lower;
            return sorted[lower] * (1.0 - frac) + sorted[upper] * frac;
        };
        
        stats.median_ns = percentile(50.0);
        stats.p50_ns = stats.median_ns;
        stats.p90_ns = percentile(90.0);
        stats.p95_ns = percentile(95.0);
        stats.p99_ns = percentile(99.0);
        stats.p999_ns = percentile(99.9);
        
        return stats;
    }
    
    /**
     * @brief Clear all samples
     */
    void clear() noexcept {
        std::lock_guard lock(mutex_);
        write_pos_ = 0;
        count_ = 0;
        min_ = std::numeric_limits<Duration>::max();
        max_ = 0;
        sum_ = 0;
    }
    
    /**
     * @brief Get sample count
     */
    [[nodiscard]] std::size_t count() const noexcept {
        std::lock_guard lock(mutex_);
        return count_;
    }
};

} // namespace ces
