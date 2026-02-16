/**
 * @file main.cpp
 * @brief Main entry point for the Concurrent Exchange Simulator
 * 
 * CLI flags:
 *   --orders N      Total orders to generate
 *   --traders T     Number of trader threads
 *   --capacity C    Ring buffer capacity (must be power of 2)
 *   --seed S        Random seed
 *   --pin           Enable thread pinning
 *   --log FILE      Log file path
 */

#include <ces/common/types.hpp>
#include <ces/common/time.hpp>
#include <ces/concurrency/spsc_semaphore_queue.hpp>
#include <ces/concurrency/pinning.hpp>
#include <ces/engine/matching_engine.hpp>
#include <ces/engine/trader.hpp>
#include <ces/logging/async_logger.hpp>

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdlib>

using namespace ces;

// Default configuration
static constexpr std::size_t DEFAULT_QUEUE_CAPACITY = 65536;  // 64K
static constexpr std::uint64_t DEFAULT_ORDERS = 10'000;
static constexpr std::size_t DEFAULT_TRADERS = 1;  // Must be 1 for SPSC queue
static constexpr std::uint64_t DEFAULT_SEED = 12345;

struct Config {
    std::uint64_t orders{DEFAULT_ORDERS};
    std::size_t traders{DEFAULT_TRADERS};
    std::uint64_t seed{DEFAULT_SEED};
    bool enable_pinning{false};
    std::string log_file;
};

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "\nOptions:\n"
              << "  --orders N      Total orders to generate (default: " << DEFAULT_ORDERS << ")\n"
              << "  --traders T     Number of trader threads (default: " << DEFAULT_TRADERS << ")\n"
              << "  --seed S        Random seed (default: " << DEFAULT_SEED << ")\n"
              << "  --pin           Enable thread pinning\n"
              << "  --log FILE      Log file path (default: none)\n"
              << "  --help          Show this help message\n";
}

Config parse_args(int argc, char* argv[]) {
    Config config;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--orders" && i + 1 < argc) {
            config.orders = std::stoull(argv[++i]);
        } else if (arg == "--traders" && i + 1 < argc) {
            config.traders = std::stoull(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            config.seed = std::stoull(argv[++i]);
        } else if (arg == "--pin") {
            config.enable_pinning = true;
        } else if (arg == "--log" && i + 1 < argc) {
            config.log_file = argv[++i];
        } else if (arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        }
    }
    
    return config;
}

int main(int argc, char* argv[]) {
    try {
        std::cout << "=== Concurrent Exchange Simulator ===\n" << std::endl;
        
        Config config = parse_args(argc, argv);
        
        std::cout << "Configuration:\n";
        std::cout << "  Orders:      " << config.orders << "\n";
        std::cout << "  Traders:     " << config.traders << "\n";
        std::cout << "  Seed:        " << config.seed << "\n";
        std::cout << "  Pinning:     " << (config.enable_pinning ? "enabled" : "disabled") << "\n";
    std::cout << "  Log file:    " << (config.log_file.empty() ? "none" : config.log_file) << "\n";
    std::cout << "  CPU cores:   " << get_num_cores() << "\n\n";
    
    // Create optional logger
    std::unique_ptr<AsyncLogger> logger;
    if (!config.log_file.empty()) {
        logger = std::make_unique<AsyncLogger>(config.log_file);
        std::cout << "Logging enabled: " << config.log_file << "\n";
    }
    
    // Create event queue
    using Queue = SpscSemaphoreQueue<OrderEvent, DEFAULT_QUEUE_CAPACITY>;
    Queue queue;
    
    // Create matching engine
    EngineConfig engine_config;
    engine_config.enable_logging = !config.log_file.empty();
    if (config.enable_pinning && get_num_cores() > 1) {
        engine_config.pin_to_core = 0;  // Pin engine to core 0
    }
    
    MatchingEngine<DEFAULT_QUEUE_CAPACITY> engine(queue, engine_config, logger.get());
    
    // Start matching engine thread
    std::cout << "Starting matching engine...\n";
    std::jthread engine_thread([&engine](std::stop_token st) {
        engine.run(st);
    });
    
    // Calculate orders per trader
    std::uint64_t orders_per_trader = config.orders / config.traders;
    std::uint64_t remaining_orders = config.orders % config.traders;
    
    // Create and start trader threads
    std::cout << "Starting " << config.traders << " trader threads...\n";
    
    std::vector<std::unique_ptr<Trader<DEFAULT_QUEUE_CAPACITY>>> traders;
    std::vector<std::jthread> trader_threads;
    
    std::uint64_t next_order_id = 1;
    
    for (std::size_t i = 0; i < config.traders; ++i) {
        TraderConfig trader_config;
        trader_config.trader_id = TraderId{static_cast<std::uint32_t>(i)};
        trader_config.seed = config.seed + i;
        trader_config.orders_to_generate = orders_per_trader + (i == 0 ? remaining_orders : 0);
        
        if (config.enable_pinning && get_num_cores() > i + 1) {
            trader_config.pin_to_core = static_cast<std::uint32_t>(i + 1);
        }
        
        traders.push_back(std::make_unique<Trader<DEFAULT_QUEUE_CAPACITY>>(
            trader_config, queue, next_order_id
        ));
        
        next_order_id += trader_config.orders_to_generate;
    }
    
    Timestamp start_time = now_ns();
    
    // Start trader threads
    for (auto& trader : traders) {
        trader_threads.emplace_back([&trader](std::stop_token st) {
            trader->run(st);
        });
    }
    
    // Wait for all traders to finish
    std::cout << "Waiting for traders to complete...\n";
    for (auto& thread : trader_threads) {
        thread.join();
    }
    
    Timestamp traders_done_time = now_ns();
    std::cout << "All traders completed.\n";
    
    // Give engine time to process remaining events
    std::cout << "Draining event queue...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Stop engine
    engine_thread.request_stop();
    engine_thread.join();
    
    Timestamp end_time = now_ns();
    
    // Print results
    double total_time_s = static_cast<double>(end_time - start_time) / 1e9;
    double trader_time_s = static_cast<double>(traders_done_time - start_time) / 1e9;
    
    std::cout << "\n=== Performance Results ===\n";
    std::cout << "Total time:         " << total_time_s << " seconds\n";
    std::cout << "Order gen time:     " << trader_time_s << " seconds\n";
    std::cout << "Orders processed:   " << engine.events_processed() << "\n";
    std::cout << "Throughput:         " << static_cast<std::uint64_t>(config.orders / total_time_s) 
              << " orders/second\n";
    
    // Print engine stats
    engine.stats().print_summary();
    
    // Print book state
    std::cout << "\n=== Final Book State ===\n";
    std::cout << "  Active orders:  " << engine.book().order_count() << "\n";
    std::cout << "  Bid levels:     " << engine.book().bid_levels() << "\n";
    std::cout << "  Ask levels:     " << engine.book().ask_levels() << "\n";
    
    auto best_bid = engine.book().best_bid();
    auto best_ask = engine.book().best_ask();
    if (best_bid) {
        std::cout << "  Best bid:       " << best_bid->get() << "\n";
    }
    if (best_ask) {
        std::cout << "  Best ask:       " << best_ask->get() << "\n";
    }
    if (auto spread = engine.book().spread()) {
        std::cout << "  Spread:         " << *spread << "\n";
    }
    
    if (logger) {
        std::cout << "\n=== Logging Stats ===\n";
        std::cout << "  Messages logged:  " << logger->messages_logged() << "\n";
        std::cout << "  Messages dropped: " << logger->messages_dropped() << "\n";
    }
    
    std::cout << "\nSimulation complete.\n";
    
    return 0;
    
    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "UNKNOWN EXCEPTION" << std::endl;
        return 1;
    }
}
