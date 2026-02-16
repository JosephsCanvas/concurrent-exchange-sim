#pragma once
/**
 * @file spsc_semaphore_queue.hpp
 * @brief Single-Producer Single-Consumer queue using counting semaphores
 * 
 * Uses std::counting_semaphore for signaling (no busy-wait loops).
 * Cache-line padded to avoid false sharing between producer and consumer.
 */

#include <ces/common/types.hpp>
#include <ces/common/macros.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <semaphore>

namespace ces {

/**
 * @brief Bounded SPSC queue using counting semaphores for coordination
 * 
 * @tparam T Element type (should be trivially copyable)
 * @tparam Capacity Queue capacity (must be power of 2)
 * 
 * Thread Safety:
 * - ONE producer thread calls push()
 * - ONE consumer thread calls pop()
 * - No mutex inside - uses semaphores for signaling
 * 
 * Semaphore Protocol:
 * - free_slots: counts available slots (starts at Capacity)
 * - filled_slots: counts items ready to consume (starts at 0)
 * 
 * Producer: free_slots.acquire() -> write -> filled_slots.release()
 * Consumer: filled_slots.acquire() -> read -> free_slots.release()
 */
template<typename T, std::size_t Capacity>
    requires (Capacity > 0 && (Capacity & (Capacity - 1)) == 0)  // Power of 2
class SpscSemaphoreQueue {
private:
    static constexpr std::size_t MASK = Capacity - 1;
    
    // Heap-allocated buffer to avoid stack overflow with large capacities
    // Cache-line aligned for performance
    std::unique_ptr<T[]> buffer_;
    
    // Producer index - only written by producer
    struct alignas(CACHE_LINE_SIZE) {
        std::atomic<std::size_t> value{0};
    } head_;
    
    // Consumer index - only written by consumer
    struct alignas(CACHE_LINE_SIZE) {
        std::atomic<std::size_t> value{0};
    } tail_;
    
    // Semaphores for coordination
    std::counting_semaphore<Capacity> free_slots_{Capacity};
    std::counting_semaphore<Capacity> filled_slots_{0};

public:
    SpscSemaphoreQueue() : buffer_(new T[Capacity]{}) {}
    ~SpscSemaphoreQueue() = default;
    
    // Non-copyable, non-movable
    SpscSemaphoreQueue(const SpscSemaphoreQueue&) = delete;
    SpscSemaphoreQueue& operator=(const SpscSemaphoreQueue&) = delete;
    SpscSemaphoreQueue(SpscSemaphoreQueue&&) = delete;
    SpscSemaphoreQueue& operator=(SpscSemaphoreQueue&&) = delete;
    
    // ========================================================================
    // Producer Interface (call from ONE thread only)
    // ========================================================================
    
    /**
     * @brief Push element (blocking if full)
     * @param value Value to push
     * 
     * Blocks until a slot is available.
     */
    void push(const T& value) noexcept {
        free_slots_.acquire();  // Wait for free slot
        
        std::size_t head = head_.value.load(std::memory_order_relaxed);
        buffer_[head & MASK] = value;
        head_.value.store(head + 1, std::memory_order_release);
        
        filled_slots_.release();  // Signal item ready
    }
    
    /**
     * @brief Push element (blocking if full) - move version
     * @param value Value to push
     */
    void push(T&& value) noexcept {
        free_slots_.acquire();
        
        std::size_t head = head_.value.load(std::memory_order_relaxed);
        buffer_[head & MASK] = std::move(value);
        head_.value.store(head + 1, std::memory_order_release);
        
        filled_slots_.release();
    }
    
    /**
     * @brief Try to push element (non-blocking)
     * @param value Value to push
     * @return true if pushed, false if queue was full
     */
    [[nodiscard]] bool try_push(const T& value) noexcept {
        if (!free_slots_.try_acquire()) {
            return false;
        }
        
        std::size_t head = head_.value.load(std::memory_order_relaxed);
        buffer_[head & MASK] = value;
        head_.value.store(head + 1, std::memory_order_release);
        
        filled_slots_.release();
        return true;
    }
    
    /**
     * @brief Try to push with timeout
     * @tparam Rep Duration rep type
     * @tparam Period Duration period type
     * @param value Value to push
     * @param timeout Maximum time to wait
     * @return true if pushed, false if timeout
     */
    template<typename Rep, typename Period>
    [[nodiscard]] bool try_push_for(const T& value, 
                                     std::chrono::duration<Rep, Period> timeout) noexcept {
        if (!free_slots_.try_acquire_for(timeout)) {
            return false;
        }
        
        std::size_t head = head_.value.load(std::memory_order_relaxed);
        buffer_[head & MASK] = value;
        head_.value.store(head + 1, std::memory_order_release);
        
        filled_slots_.release();
        return true;
    }
    
    // ========================================================================
    // Consumer Interface (call from ONE thread only)
    // ========================================================================
    
    /**
     * @brief Pop element (blocking if empty)
     * @param out Reference to store popped value
     * 
     * Blocks until an item is available.
     */
    void pop(T& out) noexcept {
        filled_slots_.acquire();  // Wait for item
        
        std::size_t tail = tail_.value.load(std::memory_order_relaxed);
        out = std::move(buffer_[tail & MASK]);
        tail_.value.store(tail + 1, std::memory_order_release);
        
        free_slots_.release();  // Signal slot free
    }
    
    /**
     * @brief Pop element (blocking if empty)
     * @return Popped value
     */
    [[nodiscard]] T pop() noexcept {
        T value;
        pop(value);
        return value;
    }
    
    /**
     * @brief Try to pop element (non-blocking)
     * @param out Reference to store popped value
     * @return true if popped, false if queue was empty
     */
    [[nodiscard]] bool try_pop(T& out) noexcept {
        if (!filled_slots_.try_acquire()) {
            return false;
        }
        
        std::size_t tail = tail_.value.load(std::memory_order_relaxed);
        out = std::move(buffer_[tail & MASK]);
        tail_.value.store(tail + 1, std::memory_order_release);
        
        free_slots_.release();
        return true;
    }
    
    /**
     * @brief Try to pop with timeout
     * @tparam Rep Duration rep type
     * @tparam Period Duration period type
     * @param out Reference to store popped value
     * @param timeout Maximum time to wait
     * @return true if popped, false if timeout
     */
    template<typename Rep, typename Period>
    [[nodiscard]] bool try_pop_for(T& out, 
                                    std::chrono::duration<Rep, Period> timeout) noexcept {
        if (!filled_slots_.try_acquire_for(timeout)) {
            return false;
        }
        
        std::size_t tail = tail_.value.load(std::memory_order_relaxed);
        out = std::move(buffer_[tail & MASK]);
        tail_.value.store(tail + 1, std::memory_order_release);
        
        free_slots_.release();
        return true;
    }
    
    // ========================================================================
    // Query Interface (can be called from any thread, approximate values)
    // ========================================================================
    
    /**
     * @brief Approximate number of items in queue
     * @note May not be exact under concurrent access
     */
    [[nodiscard]] std::size_t size_approx() const noexcept {
        std::size_t head = head_.value.load(std::memory_order_acquire);
        std::size_t tail = tail_.value.load(std::memory_order_acquire);
        return head - tail;
    }
    
    /**
     * @brief Maximum capacity
     */
    [[nodiscard]] constexpr std::size_t capacity() const noexcept {
        return Capacity;
    }
    
    /**
     * @brief Check if queue appears empty
     */
    [[nodiscard]] bool empty_approx() const noexcept {
        return size_approx() == 0;
    }
    
    /**
     * @brief Check if queue appears full
     */
    [[nodiscard]] bool full_approx() const noexcept {
        return size_approx() >= Capacity;
    }
};

} // namespace ces
