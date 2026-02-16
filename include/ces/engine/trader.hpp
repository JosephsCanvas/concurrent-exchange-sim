#pragma once
/**
 * @file trader.hpp
 * @brief Synthetic order generator (producer) for simulation
 * 
 * Generates random orders at configurable rate with deterministic seeding.
 */

#include <ces/common/types.hpp>
#include <ces/common/time.hpp>
#include <ces/lob/order.hpp>
#include <ces/concurrency/spsc_semaphore_queue.hpp>
#include <ces/concurrency/pinning.hpp>

#include <random>
#include <atomic>
#include <thread>
#include <stop_token>
#include <cstdint>

namespace ces {

/**
 * @brief Configuration for synthetic order generation
 */
struct TraderConfig {
    TraderId trader_id{TraderId{0}};
    std::uint64_t seed{12345};
    std::uint64_t orders_to_generate{1000};
    
    // Price distribution
    Price base_price{Price{10000}};  // Center price
    std::int64_t price_range{100};   // +/- range from base
    
    // Quantity distribution
    Qty min_qty{Qty{1}};
    Qty max_qty{Qty{100}};
    
    // Order type distribution (probabilities should sum to ~1.0)
    double prob_buy{0.5};
    double prob_limit{0.95};   // vs market
    double prob_cancel{0.1};   // Cancel existing order
    double prob_modify{0.05};  // Modify existing order
    
    // Rate limiting
    std::uint64_t orders_per_second{0};  // 0 = no limit
    std::uint64_t burst_size{10};        // Orders per burst
    
    // Thread affinity
    std::optional<std::uint32_t> pin_to_core;
    
    TraderConfig() = default;
};

/**
 * @brief Synthetic order generator for simulation
 * 
 * Generates random orders and pushes them to a queue.
 * Designed to run in a separate thread (std::jthread).
 */
template<std::size_t QueueCapacity>
class Trader {
public:
    using Queue = SpscSemaphoreQueue<OrderEvent, QueueCapacity>;

private:
    TraderConfig config_;
    Queue& queue_;
    
    std::mt19937_64 rng_;
    std::atomic<std::uint64_t> orders_sent_{0};
    std::atomic<bool> running_{false};
    
    // Track sent orders for cancel/modify
    std::vector<OrderId> sent_order_ids_;
    std::atomic<std::uint64_t> next_order_id_;

public:
    /**
     * @brief Construct trader with config and output queue
     * @param config Trader configuration
     * @param queue Queue to push orders to
     * @param starting_order_id Starting order ID (must be unique across traders)
     */
    Trader(TraderConfig config, Queue& queue, std::uint64_t starting_order_id)
        : config_(std::move(config))
        , queue_(queue)
        , rng_(config_.seed)
        , next_order_id_(starting_order_id) {
        
        sent_order_ids_.reserve(config_.orders_to_generate);
    }
    
    /**
     * @brief Run the order generation loop
     * 
     * This is designed to be called as a std::jthread body.
     * Uses stop_token for clean shutdown.
     */
    void run(std::stop_token stop_token) {
        running_.store(true, std::memory_order_release);
        
        // Pin thread if configured
        if (config_.pin_to_core) {
            auto result = pin_thread_to_core(*config_.pin_to_core);
            if (result != PinResult::Success) {
                // Log warning but continue
            }
        }
        
        std::uniform_real_distribution<double> unit_dist(0.0, 1.0);
        std::uniform_int_distribution<std::int64_t> price_dist(
            config_.base_price.get() - config_.price_range,
            config_.base_price.get() + config_.price_range
        );
        std::uniform_int_distribution<std::int64_t> qty_dist(
            config_.min_qty.get(),
            config_.max_qty.get()
        );
        
        // Rate limiting
        Timestamp last_burst_time = now_ns();
        std::uint64_t burst_count = 0;
        const std::uint64_t ns_per_order = config_.orders_per_second > 0
            ? 1'000'000'000ULL / config_.orders_per_second
            : 0;
        
        while (!stop_token.stop_requested() && 
               orders_sent_.load(std::memory_order_relaxed) < config_.orders_to_generate) {
            
            // Rate limiting
            if (ns_per_order > 0) {
                if (burst_count >= config_.burst_size) {
                    Timestamp now = now_ns();
                    Timestamp target = last_burst_time + (ns_per_order * config_.burst_size);
                    if (now < target) {
                        std::this_thread::sleep_for(
                            std::chrono::nanoseconds(target - now)
                        );
                    }
                    last_burst_time = now_ns();
                    burst_count = 0;
                }
            }
            
            // Generate order
            OrderEvent event = generate_order(unit_dist, price_dist, qty_dist);
            
            // Push to queue (blocks if full)
            queue_.push(event);
            
            // Track for cancel/modify
            if (event.type == OrderType::NewLimit || event.type == OrderType::NewMarket) {
                sent_order_ids_.push_back(event.order_id);
            }
            
            orders_sent_.fetch_add(1, std::memory_order_relaxed);
            ++burst_count;
        }
        
        running_.store(false, std::memory_order_release);
    }
    
    /**
     * @brief Get number of orders sent
     */
    [[nodiscard]] std::uint64_t orders_sent() const noexcept {
        return orders_sent_.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Check if still running
     */
    [[nodiscard]] bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Get trader ID
     */
    [[nodiscard]] TraderId trader_id() const noexcept {
        return config_.trader_id;
    }

private:
    template<typename D1, typename D2, typename D3>
    OrderEvent generate_order(D1& unit_dist, D2& price_dist, D3& qty_dist) {
        double r = unit_dist(rng_);
        
        // Decide order type
        bool is_cancel = r < config_.prob_cancel && !sent_order_ids_.empty();
        bool is_modify = !is_cancel && r < (config_.prob_cancel + config_.prob_modify) 
                         && !sent_order_ids_.empty();
        
        if (is_cancel) {
            // Random cancel
            std::uniform_int_distribution<std::size_t> idx_dist(0, sent_order_ids_.size() - 1);
            OrderId cancel_id = sent_order_ids_[idx_dist(rng_)];
            return OrderEvent::cancel(cancel_id);
        }
        
        if (is_modify) {
            // Random modify
            std::uniform_int_distribution<std::size_t> idx_dist(0, sent_order_ids_.size() - 1);
            OrderId modify_id = sent_order_ids_[idx_dist(rng_)];
            Qty new_qty{qty_dist(rng_)};
            Price new_price{price_dist(rng_)};
            return OrderEvent::modify(modify_id, new_qty, new_price);
        }
        
        // New order
        OrderId order_id{next_order_id_.fetch_add(1, std::memory_order_relaxed)};
        Side side = unit_dist(rng_) < config_.prob_buy ? Side::Buy : Side::Sell;
        Qty qty{qty_dist(rng_)};
        
        if (unit_dist(rng_) < config_.prob_limit) {
            Price price{price_dist(rng_)};
            return OrderEvent::new_limit(order_id, config_.trader_id, side, price, qty);
        } else {
            return OrderEvent::new_market(order_id, config_.trader_id, side, qty);
        }
    }
};

} // namespace ces
