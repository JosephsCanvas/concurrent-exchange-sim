#pragma once
/**
 * @file matching_engine.hpp
 * @brief Matching engine consumer that processes order events
 * 
 * Owns OrderBook, Accounts, and Stats.
 * Consumes events from SPSC queue and applies them to the book.
 */

#include <ces/common/types.hpp>
#include <ces/common/time.hpp>
#include <ces/common/macros.hpp>
#include <ces/lob/order_book.hpp>
#include <ces/lob/order.hpp>
#include <ces/engine/accounts.hpp>
#include <ces/engine/risk.hpp>
#include <ces/concurrency/spsc_semaphore_queue.hpp>
#include <ces/concurrency/pinning.hpp>
#include <ces/metrics/stats.hpp>
#include <ces/logging/async_logger.hpp>

#include <atomic>
#include <thread>
#include <stop_token>
#include <optional>
#include <chrono>
#include <functional>

namespace ces {

/**
 * @brief Configuration for the matching engine
 */
struct EngineConfig {
    // Order book configuration
    std::uint32_t max_orders{static_cast<std::uint32_t>(constants::DEFAULT_MAX_ORDERS)};
    std::size_t max_price_levels{constants::DEFAULT_MAX_PRICE_LEVELS};
    
    // Account configuration
    std::size_t max_traders{1000};
    std::int64_t initial_balance{1'000'000'000};  // 1 billion
    
    // Risk configuration
    RiskConfig risk;
    
    // Thread affinity
    std::optional<std::uint32_t> pin_to_core;
    
    // Logging
    bool enable_logging{false};
    std::string log_file{"engine.log"};
    
    EngineConfig() = default;
};

/**
 * @brief Matching engine that consumes order events and maintains the book
 * 
 * Thread Safety:
 * - Single consumer thread reads from queue and updates book
 * - Uses std::jthread with stop_token for clean shutdown
 * - Stats updated via std::atomic_ref for thread-safe reads
 * 
 * @tparam QueueCapacity Capacity of input queue (must be power of 2)
 */
template<std::size_t QueueCapacity>
class MatchingEngine {
public:
    using Queue = SpscSemaphoreQueue<OrderEvent, QueueCapacity>;

private:
    Queue& queue_;
    OrderBook book_;
    Accounts accounts_;
    RiskChecker risk_;
    EngineStats stats_;
    AsyncLogger* logger_;
    EngineConfig config_;
    
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> events_processed_{0};

public:
    /**
     * @brief Construct matching engine
     * @param queue Input event queue
     * @param config Engine configuration
     * @param logger Optional async logger
     */
    MatchingEngine(Queue& queue, EngineConfig config = {}, AsyncLogger* logger = nullptr)
        : queue_(queue)
        , book_(config.max_orders, config.max_price_levels)
        , accounts_(config.max_traders)
        , risk_(config.risk, &accounts_)
        , logger_(logger)
        , config_(std::move(config)) {
        
        // Set up trade callback to update accounts
        book_.set_trade_callback([this](const Trade& trade) {
            on_trade(trade);
        });
    }
    
    ~MatchingEngine() = default;
    
    // Non-copyable, non-movable
    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;
    
    /**
     * @brief Run the matching engine loop
     * 
     * Designed to be called as std::jthread body.
     * Blocks on queue.pop() until stop is requested.
     */
    void run(std::stop_token stop_token) {
        running_.store(true, std::memory_order_release);
        
        // Pin thread if configured
        if (config_.pin_to_core) {
            [[maybe_unused]] auto pin_result = pin_thread_to_core(*config_.pin_to_core);
        }
        
        OrderEvent event;
        
        while (!stop_token.stop_requested()) {
            // Try to pop with timeout to check stop_token periodically
            bool popped = queue_.try_pop_for(event, std::chrono::milliseconds(10));
            if (!popped) {
                continue;
            }
            
            process_event(event);
        }
        
        // Drain remaining events
        while (queue_.try_pop(event)) {
            process_event(event);
        }
        
        running_.store(false, std::memory_order_release);
    }
    
    /**
     * @brief Process single event (exposed for testing)
     */
    void process_event(const OrderEvent& event) {
        Timestamp start = now_ns();
        
        // Ensure trader account exists
        if (event.type != OrderType::Cancel) {
            accounts_.get_or_create(event.trader_id, config_.initial_balance);
        }
        
        // Risk check
        RiskResult risk_result = risk_.check(event);
        if CES_UNLIKELY(risk_result != RiskResult::Passed) {
            stats_.rejected_count.fetch_add(1, std::memory_order_relaxed);
            if (logger_) {
                logger_->log("Rejected order {} reason: {}", 
                            event.order_id.get(), to_string(risk_result));
            }
            record_latency(event.enqueue_time, start);
            return;
        }
        
        // Process based on type
        OrderResponse response;
        
        switch (event.type) {
            case OrderType::NewLimit:
                response = book_.add_limit(
                    event.order_id, event.trader_id,
                    event.side, event.price, event.qty
                );
                break;
                
            case OrderType::NewMarket:
                response = book_.add_market(
                    event.order_id, event.trader_id,
                    event.side, event.qty
                );
                break;
                
            case OrderType::Cancel:
                response = book_.cancel(event.order_id);
                break;
                
            case OrderType::Modify:
                response = book_.modify(event.order_id, event.qty, event.price);
                break;
        }
        
        // Update stats
        events_processed_.fetch_add(1, std::memory_order_relaxed);
        
        if (response.success()) {
            if (response.qty_filled.get() > 0) {
                stats_.filled_qty.fetch_add(response.qty_filled.get(), std::memory_order_relaxed);
            }
        }
        
        record_latency(event.enqueue_time, start);
    }
    
    // ========================================================================
    // Accessors
    // ========================================================================
    
    /**
     * @brief Get reference to order book
     */
    [[nodiscard]] OrderBook& book() noexcept { return book_; }
    [[nodiscard]] const OrderBook& book() const noexcept { return book_; }
    
    /**
     * @brief Get reference to accounts
     */
    [[nodiscard]] Accounts& accounts() noexcept { return accounts_; }
    [[nodiscard]] const Accounts& accounts() const noexcept { return accounts_; }
    
    /**
     * @brief Get reference to stats
     */
    [[nodiscard]] EngineStats& stats() noexcept { return stats_; }
    [[nodiscard]] const EngineStats& stats() const noexcept { return stats_; }
    
    /**
     * @brief Get events processed count
     */
    [[nodiscard]] std::uint64_t events_processed() const noexcept {
        return events_processed_.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Check if engine is running
     */
    [[nodiscard]] bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

private:
    /**
     * @brief Handle trade execution
     */
    void on_trade(const Trade& trade) {
        // Update accounts
        accounts_.apply_trade(
            trade.maker_trader_id,
            trade.taker_trader_id,
            trade.taker_side,
            trade.price,
            trade.qty
        );
        
        // Update stats
        stats_.trade_count.fetch_add(1, std::memory_order_relaxed);
        stats_.volume.fetch_add(trade.qty.get(), std::memory_order_relaxed);
        
        if (logger_) {
            logger_->log("Trade: {} @ {} maker={} taker={}",
                        trade.qty.get(), trade.price.get(),
                        trade.maker_trader_id.get(), trade.taker_trader_id.get());
        }
    }
    
    /**
     * @brief Record latency sample
     */
    void record_latency(Timestamp enqueue_time, Timestamp process_start) {
        Timestamp now = now_ns();
        Duration total_latency = static_cast<Duration>(now - enqueue_time);
        Duration process_latency = static_cast<Duration>(now - process_start);
        
        stats_.record_latency(total_latency);
        (void)process_latency;  // Could track separately
    }
};

} // namespace ces
