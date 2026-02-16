#pragma once
/**
 * @file ring_buffer.hpp
 * @brief Basic ring buffer without synchronization primitives
 * 
 * Used as the underlying storage for synchronized queue implementations.
 */

#include <ces/common/types.hpp>
#include <ces/common/macros.hpp>

#include <array>
#include <cstddef>
#include <optional>

namespace ces {

/**
 * @brief Fixed-capacity ring buffer (circular buffer)
 * 
 * @tparam T Element type (should be trivially copyable for best performance)
 * @tparam Capacity Buffer capacity (must be power of 2 for efficient modulo)
 * 
 * Thread Safety: NOT thread-safe. Use with external synchronization.
 */
template<typename T, std::size_t Capacity>
    requires (Capacity > 0 && (Capacity & (Capacity - 1)) == 0)  // Power of 2
class RingBuffer {
private:
    static constexpr std::size_t MASK = Capacity - 1;
    
    CES_CACHE_ALIGNED std::array<T, Capacity> buffer_{};
    std::size_t head_{0};  // Next write position
    std::size_t tail_{0};  // Next read position
    std::size_t size_{0};

public:
    RingBuffer() = default;
    ~RingBuffer() = default;
    
    // Non-copyable, non-movable for simplicity
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    
    /**
     * @brief Push element to back of buffer
     * @param value Value to push
     * @return true if successful, false if full
     */
    [[nodiscard]] bool push(const T& value) noexcept {
        if CES_UNLIKELY(size_ >= Capacity) {
            return false;
        }
        
        buffer_[head_] = value;
        head_ = (head_ + 1) & MASK;
        ++size_;
        return true;
    }
    
    /**
     * @brief Push element to back of buffer (move)
     * @param value Value to push
     * @return true if successful, false if full
     */
    [[nodiscard]] bool push(T&& value) noexcept {
        if CES_UNLIKELY(size_ >= Capacity) {
            return false;
        }
        
        buffer_[head_] = std::move(value);
        head_ = (head_ + 1) & MASK;
        ++size_;
        return true;
    }
    
    /**
     * @brief Pop element from front of buffer
     * @return Popped element, or std::nullopt if empty
     */
    [[nodiscard]] std::optional<T> pop() noexcept {
        if CES_UNLIKELY(size_ == 0) {
            return std::nullopt;
        }
        
        T value = std::move(buffer_[tail_]);
        tail_ = (tail_ + 1) & MASK;
        --size_;
        return value;
    }
    
    /**
     * @brief Pop element from front into provided reference
     * @param out Reference to store popped value
     * @return true if successful, false if empty
     */
    [[nodiscard]] bool pop(T& out) noexcept {
        if CES_UNLIKELY(size_ == 0) {
            return false;
        }
        
        out = std::move(buffer_[tail_]);
        tail_ = (tail_ + 1) & MASK;
        --size_;
        return true;
    }
    
    /**
     * @brief Peek at front element without removing
     * @return Pointer to front element, or nullptr if empty
     */
    [[nodiscard]] const T* peek() const noexcept {
        if CES_UNLIKELY(size_ == 0) {
            return nullptr;
        }
        return &buffer_[tail_];
    }
    
    /**
     * @brief Current number of elements
     */
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    
    /**
     * @brief Maximum capacity
     */
    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return Capacity; }
    
    /**
     * @brief Check if buffer is empty
     */
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    
    /**
     * @brief Check if buffer is full
     */
    [[nodiscard]] bool full() const noexcept { return size_ >= Capacity; }
    
    /**
     * @brief Clear all elements
     */
    void clear() noexcept {
        head_ = 0;
        tail_ = 0;
        size_ = 0;
    }
};

} // namespace ces
