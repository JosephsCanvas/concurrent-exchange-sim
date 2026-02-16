/**
 * @file latency.cpp
 * @brief Implementation of latency statistics printing
 */

#include <ces/metrics/latency.hpp>
#include <ces/metrics/stats.hpp>

#include <iostream>
#include <iomanip>

namespace ces {

void LatencyStats::print() const {
    std::cout << "\n=== Latency Statistics ===\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Samples:  " << count << "\n";
    std::cout << "  Mean:     " << (mean_ns / 1000.0) << " µs\n";
    std::cout << "  Median:   " << (median_ns / 1000.0) << " µs\n";
    std::cout << "  P90:      " << (p90_ns / 1000.0) << " µs\n";
    std::cout << "  P95:      " << (p95_ns / 1000.0) << " µs\n";
    std::cout << "  P99:      " << (p99_ns / 1000.0) << " µs\n";
    std::cout << "  P99.9:    " << (p999_ns / 1000.0) << " µs\n";
    std::cout << "  Min:      " << (static_cast<double>(min_ns) / 1000.0) << " µs\n";
    std::cout << "  Max:      " << (static_cast<double>(max_ns) / 1000.0) << " µs\n";
    std::cout << "===========================\n";
}

void EngineStats::print_summary() const {
    std::cout << "\n=== Engine Statistics ===\n";
    std::cout << "  Trades:       " << trade_count.load() << "\n";
    std::cout << "  Volume:       " << volume.load() << "\n";
    std::cout << "  Orders Recv:  " << orders_received.load() << "\n";
    std::cout << "  Accepted:     " << orders_accepted.load() << "\n";
    std::cout << "  Cancelled:    " << orders_cancelled.load() << "\n";
    std::cout << "  Modified:     " << orders_modified.load() << "\n";
    std::cout << "  Rejected:     " << rejected_count.load() << "\n";
    std::cout << "  Filled Qty:   " << filled_qty.load() << "\n";
    std::cout << "=========================\n";
    
    auto latency_stats = get_latency_stats();
    latency_stats.print();
}

} // namespace ces
