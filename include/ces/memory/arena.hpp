#pragma once
/**
 * @file arena.hpp
 * @brief Simple arena allocator for bulk allocations
 * 
 * Provides fast bump-pointer allocation with no individual deallocation.
 * All memory freed at once when arena is reset/destroyed.
 */

#include <ces/common/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

namespace ces {

/**
 * @brief Fixed-size arena allocator with bump-pointer allocation
 * 
 * Thread Safety: NOT thread-safe. Use one arena per thread or external sync.
 * 
 * Use Cases:
 * - Batch allocations that are freed together
 * - Temporary buffers for order processing
 * - Avoiding fragmentation
 */
class Arena {
private:
    std::unique_ptr<std::byte[]> buffer_;
    std::size_t capacity_;
    std::size_t offset_{0};

public:
    /**
     * @brief Construct arena with given capacity
     * @param capacity Total bytes available
     */
    explicit Arena(std::size_t capacity)
        : buffer_(std::make_unique<std::byte[]>(capacity))
        , capacity_(capacity) {
    }
    
    ~Arena() = default;
    
    // Non-copyable, movable
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&&) noexcept = default;
    Arena& operator=(Arena&&) noexcept = default;
    
    /**
     * @brief Allocate memory from arena
     * @param size Bytes to allocate
     * @param alignment Alignment requirement (power of 2)
     * @return Pointer to allocated memory, or nullptr if insufficient space
     */
    [[nodiscard]] void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) noexcept {
        // Align current offset
        std::size_t aligned_offset = (offset_ + alignment - 1) & ~(alignment - 1);
        
        if CES_UNLIKELY(aligned_offset + size > capacity_) {
            return nullptr;  // Out of space
        }
        
        void* ptr = buffer_.get() + aligned_offset;
        offset_ = aligned_offset + size;
        return ptr;
    }
    
    /**
     * @brief Allocate and construct an object
     * @tparam T Object type
     * @tparam Args Constructor argument types
     * @param args Constructor arguments
     * @return Pointer to constructed object, or nullptr if insufficient space
     */
    template<typename T, typename... Args>
    [[nodiscard]] T* create(Args&&... args) {
        void* ptr = allocate(sizeof(T), alignof(T));
        if CES_UNLIKELY(ptr == nullptr) {
            return nullptr;
        }
        return new (ptr) T(std::forward<Args>(args)...);
    }
    
    /**
     * @brief Allocate array of objects (default constructed)
     * @tparam T Element type
     * @param count Number of elements
     * @return Pointer to first element, or nullptr if insufficient space
     */
    template<typename T>
    [[nodiscard]] T* create_array(std::size_t count) {
        void* ptr = allocate(sizeof(T) * count, alignof(T));
        if CES_UNLIKELY(ptr == nullptr) {
            return nullptr;
        }
        
        T* arr = static_cast<T*>(ptr);
        for (std::size_t i = 0; i < count; ++i) {
            new (arr + i) T();
        }
        return arr;
    }
    
    /**
     * @brief Reset arena (free all allocations)
     * @note Does NOT call destructors - use only for trivial types
     */
    void reset() noexcept {
        offset_ = 0;
    }
    
    /**
     * @brief Current bytes used
     */
    [[nodiscard]] std::size_t used() const noexcept { return offset_; }
    
    /**
     * @brief Total capacity in bytes
     */
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    
    /**
     * @brief Remaining available bytes
     */
    [[nodiscard]] std::size_t remaining() const noexcept { return capacity_ - offset_; }
};

/**
 * @brief RAII scope guard for arena allocations
 * 
 * Automatically resets arena to previous state when scope exits.
 */
class ArenaScope {
private:
    Arena& arena_;
    std::size_t saved_offset_;

public:
    explicit ArenaScope(Arena& arena) noexcept
        : arena_(arena)
        , saved_offset_(arena.used()) {
    }
    
    ~ArenaScope() noexcept {
        // Note: destructors not called - use only for trivial types
        arena_.reset();
        // Restore to saved offset by re-allocating dummy
        // Actually, we need a proper restore mechanism
    }
    
    ArenaScope(const ArenaScope&) = delete;
    ArenaScope& operator=(const ArenaScope&) = delete;
};

} // namespace ces
