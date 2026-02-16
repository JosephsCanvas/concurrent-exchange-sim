#pragma once
/**
 * @file risk.hpp
 * @brief Simple risk checks for order validation
 * 
 * Implements basic pre-trade risk checks without blocking hot path.
 */

#include <ces/common/types.hpp>
#include <ces/engine/accounts.hpp>
#include <ces/lob/order.hpp>

#include <cstdint>

namespace ces {

/**
 * @brief Risk check configuration
 */
struct RiskConfig {
    std::int64_t max_order_value{1'000'000'000};  // Max notional per order
    std::int64_t max_position{1'000'000};         // Max position size
    Qty max_order_qty{Qty{100'000}};              // Max quantity per order
    Price max_price{Price{1'000'000}};            // Max valid price
    Price min_price{Price{1}};                    // Min valid price
    bool check_balance{true};                     // Require sufficient balance
    
    RiskConfig() = default;
};

/**
 * @brief Risk check result
 */
enum class RiskResult : std::uint8_t {
    Passed = 0,
    InvalidPrice = 1,
    InvalidQty = 2,
    ExceedsMaxOrderValue = 3,
    ExceedsMaxPosition = 4,
    InsufficientBalance = 5,
    UnknownTrader = 6
};

[[nodiscard]] constexpr const char* to_string(RiskResult r) noexcept {
    switch (r) {
        case RiskResult::Passed:              return "Passed";
        case RiskResult::InvalidPrice:        return "InvalidPrice";
        case RiskResult::InvalidQty:          return "InvalidQty";
        case RiskResult::ExceedsMaxOrderValue: return "ExceedsMaxOrderValue";
        case RiskResult::ExceedsMaxPosition:  return "ExceedsMaxPosition";
        case RiskResult::InsufficientBalance: return "InsufficientBalance";
        case RiskResult::UnknownTrader:       return "UnknownTrader";
    }
    return "Unknown";
}

/**
 * @brief Simple risk checker for pre-trade validation
 * 
 * Performs fast validation on incoming orders before they reach the book.
 * Designed to fail fast on obviously bad orders.
 */
class RiskChecker {
private:
    RiskConfig config_;
    const Accounts* accounts_;

public:
    /**
     * @brief Construct risk checker
     * @param config Risk configuration
     * @param accounts Account manager (for balance checks)
     */
    explicit RiskChecker(RiskConfig config = {}, const Accounts* accounts = nullptr)
        : config_(std::move(config))
        , accounts_(accounts) {
    }
    
    /**
     * @brief Set accounts reference
     */
    void set_accounts(const Accounts* accounts) noexcept {
        accounts_ = accounts;
    }
    
    /**
     * @brief Check if order passes risk limits
     * @param event Order event to validate
     * @return Risk check result
     */
    [[nodiscard]] RiskResult check(const OrderEvent& event) const noexcept {
        // Skip checks for cancels
        if CES_LIKELY(event.type == OrderType::Cancel) {
            return RiskResult::Passed;
        }
        
        // Price validation (skip for market orders)
        if (event.type == OrderType::NewLimit || event.type == OrderType::Modify) {
            if CES_UNLIKELY(event.price < config_.min_price || event.price > config_.max_price) {
                return RiskResult::InvalidPrice;
            }
        }
        
        // Quantity validation
        if CES_UNLIKELY(event.qty.get() <= 0 || event.qty > config_.max_order_qty) {
            return RiskResult::InvalidQty;
        }
        
        // Notional value check
        std::int64_t notional = event.price.get() * event.qty.get();
        if CES_UNLIKELY(notional > config_.max_order_value) {
            return RiskResult::ExceedsMaxOrderValue;
        }
        
        // Balance check (if enabled and accounts available)
        if (config_.check_balance && accounts_ != nullptr) {
            if (event.side == Side::Buy) {
                if (!accounts_->has_sufficient_balance(event.trader_id, notional)) {
                    return RiskResult::InsufficientBalance;
                }
            }
        }
        
        return RiskResult::Passed;
    }
    
    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const RiskConfig& config() const noexcept { return config_; }
    
    /**
     * @brief Update configuration
     */
    void set_config(RiskConfig config) noexcept { config_ = std::move(config); }
};

} // namespace ces
