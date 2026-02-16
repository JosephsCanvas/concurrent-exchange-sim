#pragma once
/**
 * @file accounts.hpp
 * @brief Thread-safe account management with striped mutex scheme
 * 
 * Solves the "ATM problem" - concurrent access to shared account state
 * without global lock contention.
 */

#include <ces/common/types.hpp>
#include <ces/common/macros.hpp>

#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <memory>

namespace ces {

/**
 * @brief Individual trader account state
 * 
 * Note: Uses alignas to ensure atomic operations work correctly.
 * Account is not directly copyable/movable due to atomics in the struct,
 * so we use unique_ptr in the Accounts container.
 */
struct alignas(CACHE_LINE_SIZE) Account {
    TraderId trader_id{constants::INVALID_TRADER_ID};
    std::atomic<std::int64_t> balance{0};
    std::atomic<std::int64_t> position{0};  // Net position (positive = long)
    std::atomic<std::uint64_t> trade_count{0};
    std::atomic<std::uint64_t> volume{0};
    
    Account() = default;
    
    explicit Account(TraderId id, std::int64_t initial_balance = 0)
        : trader_id(id)
        , balance(initial_balance) {
    }
    
    // Non-copyable due to atomics
    Account(const Account&) = delete;
    Account& operator=(const Account&) = delete;
    
    // Non-movable due to atomics  
    Account(Account&&) = delete;
    Account& operator=(Account&&) = delete;
};

/**
 * @brief Thread-safe account manager using striped mutex scheme
 * 
 * Uses multiple mutexes to reduce contention when multiple threads
 * access different accounts concurrently.
 * 
 * Thread Safety:
 * - All operations are thread-safe
 * - Uses striped locking for balance updates
 * - Atomics for read-only statistics
 */
class Accounts {
public:
    /// Default number of stripe mutexes
    static constexpr std::size_t DEFAULT_STRIPE_COUNT = 16;

private:
    std::vector<std::unique_ptr<Account>> accounts_;
    std::vector<std::mutex> stripe_mutexes_;
    std::size_t stripe_count_;
    std::size_t max_traders_;

public:
    /**
     * @brief Construct account manager
     * @param max_traders Maximum number of traders
     * @param stripe_count Number of stripe mutexes (power of 2 recommended)
     */
    explicit Accounts(
        std::size_t max_traders,
        std::size_t stripe_count = DEFAULT_STRIPE_COUNT
    )
        : stripe_mutexes_(stripe_count)
        , stripe_count_(stripe_count)
        , max_traders_(max_traders) {
        
        accounts_.reserve(max_traders);
    }
    
    ~Accounts() = default;
    
    // Non-copyable, non-movable (owns mutexes)
    Accounts(const Accounts&) = delete;
    Accounts& operator=(const Accounts&) = delete;
    
    /**
     * @brief Create a new account
     * @param trader_id Trader ID
     * @param initial_balance Starting balance
     * @return true if created, false if already exists or limit reached
     */
    bool create_account(TraderId trader_id, std::int64_t initial_balance = 0);
    
    /**
     * @brief Get or create account
     * @param trader_id Trader ID
     * @param initial_balance Balance if creating new
     * @return Pointer to account
     */
    Account* get_or_create(TraderId trader_id, std::int64_t initial_balance = 0);
    
    /**
     * @brief Get existing account
     * @param trader_id Trader ID
     * @return Pointer to account, or nullptr if not found
     */
    Account* get(TraderId trader_id);
    const Account* get(TraderId trader_id) const;
    
    /**
     * @brief Apply a trade to both maker and taker accounts
     * 
     * Atomically updates balances and positions for both parties.
     * Maker sells at trade price, taker buys (or vice versa).
     * 
     * @param maker_id Maker trader ID
     * @param taker_id Taker trader ID
     * @param taker_side Side of the taker (Buy = taker is buying)
     * @param price Trade price
     * @param qty Trade quantity
     */
    void apply_trade(
        TraderId maker_id,
        TraderId taker_id,
        Side taker_side,
        Price price,
        Qty qty
    );
    
    /**
     * @brief Adjust balance (deposit/withdrawal)
     * @param trader_id Trader ID
     * @param amount Amount to add (negative for withdrawal)
     * @return true if successful, false if account not found
     */
    bool adjust_balance(TraderId trader_id, std::int64_t amount);
    
    /**
     * @brief Check if balance is sufficient for order
     * @param trader_id Trader ID
     * @param required_amount Required balance
     * @return true if sufficient
     */
    bool has_sufficient_balance(TraderId trader_id, std::int64_t required_amount) const;
    
    /**
     * @brief Get current balance
     */
    std::int64_t get_balance(TraderId trader_id) const;
    
    /**
     * @brief Get current position
     */
    std::int64_t get_position(TraderId trader_id) const;
    
    /**
     * @brief Get total number of accounts
     */
    std::size_t size() const noexcept { return accounts_.size(); }
    
    /**
     * @brief Reset all accounts
     */
    void clear();

private:
    /**
     * @brief Get stripe index for trader ID
     */
    [[nodiscard]] std::size_t stripe_index(TraderId trader_id) const noexcept {
        return static_cast<std::size_t>(trader_id.get()) % stripe_count_;
    }
    
    /**
     * @brief Get mutex for trader
     */
    [[nodiscard]] std::mutex& get_mutex(TraderId trader_id) {
        return stripe_mutexes_[stripe_index(trader_id)];
    }
};

} // namespace ces
