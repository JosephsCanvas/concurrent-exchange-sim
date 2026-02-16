#pragma once
/**
 * @file object_pool.hpp
 * @brief Fixed-capacity object pool with O(1) allocate/free using freelist indices
 * 
 * No heap allocation after construction. Uses intrusive freelist for O(1) operations.
 */

// Prevent Windows min/max macros from conflicting with std::numeric_limits
#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <ces/common/types.hpp>
#include <ces/common/macros.hpp>
#include <ces/common/concepts.hpp>

#include <memory>
#include <cstdint>
#include <new>

namespace ces {

/**
 * @brief Fixed-capacity object pool with freelist-based allocation
 * 
 * @tparam T Type of objects to store (must be Poolable)
 * 
 * Thread Safety: NOT thread-safe. External synchronization required.
 * 
 * Memory Layout:
 * - Objects stored contiguously in array (allocated at construction)
 * - Freelist uses indices, not pointers, for cache efficiency
 * - No dynamic allocation after construction
 */
template<Poolable T>
class ObjectPool {
public:
    /// Sentinel value for invalid/end-of-list
    static constexpr std::uint32_t INVALID_INDEX = std::numeric_limits<std::uint32_t>::max();

private:
    /// Storage entry: either holds an object or a freelist link
    struct Entry {
        alignas(T) unsigned char storage[sizeof(T)];
        std::uint32_t next_free{INVALID_INDEX};
        bool in_use{false};
        
        Entry() noexcept = default;
        ~Entry() {
            if (in_use) {
                reinterpret_cast<T*>(storage)->~T();
            }
        }
        
        // Non-copyable, non-movable
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;
        Entry(Entry&&) = delete;
        Entry& operator=(Entry&&) = delete;
        
        T* get_object() noexcept { return reinterpret_cast<T*>(storage); }
        const T* get_object() const noexcept { return reinterpret_cast<const T*>(storage); }
    };
    
    std::unique_ptr<Entry[]> storage_;
    std::uint32_t free_head_{INVALID_INDEX};
    std::uint32_t capacity_{0};
    std::uint32_t size_{0};

public:
    /**
     * @brief Construct pool with given capacity
     * @param capacity Maximum number of objects (fixed, no growth)
     */
    explicit ObjectPool(std::uint32_t capacity) 
        : storage_(std::make_unique<Entry[]>(capacity))
        , capacity_(capacity) {
        
        // Build freelist
        for (std::uint32_t i = 0; i < capacity; ++i) {
            storage_[i].next_free = (i + 1 < capacity) ? (i + 1) : INVALID_INDEX;
        }
        
        free_head_ = (capacity > 0) ? 0 : INVALID_INDEX;
    }
    
    ~ObjectPool() = default;
    
    // Non-copyable, non-movable
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;
    
    /**
     * @brief Allocate an object from the pool
     * @tparam Args Constructor argument types
     * @param args Arguments forwarded to T's constructor
     * @return Index of allocated object, or INVALID_INDEX if pool exhausted
     */
    template<typename... Args>
    [[nodiscard]] std::uint32_t allocate(Args&&... args) noexcept(
        std::is_nothrow_constructible_v<T, Args...>
    ) {
        if (free_head_ == INVALID_INDEX) [[unlikely]] {
            return INVALID_INDEX;  // Pool exhausted
        }
        
        std::uint32_t idx = free_head_;
        Entry& entry = storage_[idx];
        
        // Remove from freelist
        free_head_ = entry.next_free;
        
        // Construct object in-place
        new (entry.storage) T(std::forward<Args>(args)...);
        entry.in_use = true;
        ++size_;
        
        return idx;
    }
    
    /**
     * @brief Deallocate an object and return it to the pool
     * @param index Index of object to deallocate
     */
    void deallocate(std::uint32_t index) noexcept {
        CES_ASSERT(index < capacity_);
        CES_ASSERT(storage_[index].in_use);
        
        Entry& entry = storage_[index];
        
        // Destroy object
        entry.get_object()->~T();
        entry.in_use = false;
        
        // Add to freelist head
        entry.next_free = free_head_;
        free_head_ = index;
        --size_;
    }
    
    /**
     * @brief Access object at index (unchecked in release)
     */
    [[nodiscard]] CES_FORCE_INLINE T& operator[](std::uint32_t index) noexcept {
        CES_ASSERT(index < capacity_);
        CES_ASSERT(storage_[index].in_use);
        return *storage_[index].get_object();
    }
    
    [[nodiscard]] CES_FORCE_INLINE const T& operator[](std::uint32_t index) const noexcept {
        CES_ASSERT(index < capacity_);
        CES_ASSERT(storage_[index].in_use);
        return *storage_[index].get_object();
    }
    
    /**
     * @brief Get pointer to object at index
     * @param index Object index
     * @return Pointer to object, or nullptr if not in use
     */
    [[nodiscard]] T* get(std::uint32_t index) noexcept {
        if (index >= capacity_ || !storage_[index].in_use) {
            return nullptr;
        }
        return storage_[index].get_object();
    }
    
    [[nodiscard]] const T* get(std::uint32_t index) const noexcept {
        if (index >= capacity_ || !storage_[index].in_use) {
            return nullptr;
        }
        return storage_[index].get_object();
    }
    
    /**
     * @brief Check if index is valid and in use
     */
    [[nodiscard]] bool is_valid(std::uint32_t index) const noexcept {
        return index < capacity_ && storage_[index].in_use;
    }
    
    /**
     * @brief Current number of allocated objects
     */
    [[nodiscard]] std::uint32_t size() const noexcept { return size_; }
    
    /**
     * @brief Maximum capacity
     */
    [[nodiscard]] std::uint32_t capacity() const noexcept { return capacity_; }
    
    /**
     * @brief Check if pool is full
     */
    [[nodiscard]] bool full() const noexcept { return free_head_ == INVALID_INDEX; }
    
    /**
     * @brief Check if pool is empty
     */
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    
    /**
     * @brief Clear all objects from pool
     */
    void clear() noexcept {
        for (std::uint32_t i = 0; i < capacity_; ++i) {
            if (storage_[i].in_use) {
                storage_[i].get_object()->~T();
                storage_[i].in_use = false;
            }
            storage_[i].next_free = (i + 1 < capacity_) ? (i + 1) : INVALID_INDEX;
        }
        free_head_ = (capacity_ > 0) ? 0 : INVALID_INDEX;
        size_ = 0;
    }
};

} // namespace ces
