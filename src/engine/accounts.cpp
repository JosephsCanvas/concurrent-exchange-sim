/**
 * @file accounts.cpp
 * @brief Implementation of thread-safe account management
 */

#include <ces/engine/accounts.hpp>
#include <ces/common/macros.hpp>

#include <algorithm>

namespace ces {

bool Accounts::create_account(TraderId trader_id, std::int64_t initial_balance) {
    std::lock_guard lock(get_mutex(trader_id));
    
    // Check if exists
    for (const auto& acc : accounts_) {
        if (acc->trader_id == trader_id) {
            return false;  // Already exists
        }
    }
    
    if (accounts_.size() >= max_traders_) {
        return false;  // At capacity
    }
    
    accounts_.push_back(std::make_unique<Account>(trader_id, initial_balance));
    return true;
}

Account* Accounts::get_or_create(TraderId trader_id, std::int64_t initial_balance) {
    // First, try to find existing (without lock for hot path)
    for (auto& acc : accounts_) {
        if (acc->trader_id == trader_id) {
            return acc.get();
        }
    }
    
    // Need to create - take lock
    std::lock_guard lock(get_mutex(trader_id));
    
    // Double-check after acquiring lock
    for (auto& acc : accounts_) {
        if (acc->trader_id == trader_id) {
            return acc.get();
        }
    }
    
    if (accounts_.size() >= max_traders_) {
        return nullptr;
    }
    
    accounts_.push_back(std::make_unique<Account>(trader_id, initial_balance));
    return accounts_.back().get();
}

Account* Accounts::get(TraderId trader_id) {
    for (auto& acc : accounts_) {
        if (acc->trader_id == trader_id) {
            return acc.get();
        }
    }
    return nullptr;
}

const Account* Accounts::get(TraderId trader_id) const {
    for (const auto& acc : accounts_) {
        if (acc->trader_id == trader_id) {
            return acc.get();
        }
    }
    return nullptr;
}

void Accounts::apply_trade(
    TraderId maker_id,
    TraderId taker_id,
    Side taker_side,
    Price price,
    Qty qty
) {
    Account* maker = get(maker_id);
    Account* taker = get(taker_id);
    
    if CES_UNLIKELY(!maker || !taker) {
        return;  // Should not happen in normal operation
    }
    
    std::int64_t notional = price.get() * qty.get();
    std::int64_t qty_val = qty.get();
    
    // Determine who is buying/selling
    // Taker side is the aggressor
    if (taker_side == Side::Buy) {
        // Taker buys, maker sells
        taker->balance.fetch_sub(notional, std::memory_order_relaxed);
        taker->position.fetch_add(qty_val, std::memory_order_relaxed);
        
        maker->balance.fetch_add(notional, std::memory_order_relaxed);
        maker->position.fetch_sub(qty_val, std::memory_order_relaxed);
    } else {
        // Taker sells, maker buys
        taker->balance.fetch_add(notional, std::memory_order_relaxed);
        taker->position.fetch_sub(qty_val, std::memory_order_relaxed);
        
        maker->balance.fetch_sub(notional, std::memory_order_relaxed);
        maker->position.fetch_add(qty_val, std::memory_order_relaxed);
    }
    
    // Update trade counts
    maker->trade_count.fetch_add(1, std::memory_order_relaxed);
    maker->volume.fetch_add(qty_val, std::memory_order_relaxed);
    taker->trade_count.fetch_add(1, std::memory_order_relaxed);
    taker->volume.fetch_add(qty_val, std::memory_order_relaxed);
}

bool Accounts::adjust_balance(TraderId trader_id, std::int64_t amount) {
    Account* acc = get(trader_id);
    if (!acc) {
        return false;
    }
    
    acc->balance.fetch_add(amount, std::memory_order_relaxed);
    return true;
}

bool Accounts::has_sufficient_balance(TraderId trader_id, std::int64_t required_amount) const {
    const Account* acc = get(trader_id);
    if (!acc) {
        return false;
    }
    
    return acc->balance.load(std::memory_order_relaxed) >= required_amount;
}

std::int64_t Accounts::get_balance(TraderId trader_id) const {
    const Account* acc = get(trader_id);
    if (!acc) {
        return 0;
    }
    return acc->balance.load(std::memory_order_relaxed);
}

std::int64_t Accounts::get_position(TraderId trader_id) const {
    const Account* acc = get(trader_id);
    if (!acc) {
        return 0;
    }
    return acc->position.load(std::memory_order_relaxed);
}

void Accounts::clear() {
    // Lock all stripes
    for (auto& mutex : stripe_mutexes_) {
        mutex.lock();
    }
    
    accounts_.clear();
    
    for (auto& mutex : stripe_mutexes_) {
        mutex.unlock();
    }
}

} // namespace ces
