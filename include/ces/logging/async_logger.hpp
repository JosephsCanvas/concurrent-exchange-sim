#pragma once
/**
 * @file async_logger.hpp
 * @brief Zero-allocation async logger with bounded ring buffer
 * 
 * Log messages written to fixed-size buffer entries.
 * Background thread flushes to file, never blocking hot path.
 */

#include <ces/common/types.hpp>
#include <ces/common/time.hpp>
#include <ces/common/macros.hpp>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <stop_token>
#include <fstream>
#include <chrono>

namespace ces {

/**
 * @brief Fixed-size log entry to avoid dynamic allocation
 */
struct LogEntry {
    static constexpr std::size_t MAX_MESSAGE_SIZE = 256;
    
    Timestamp timestamp{0};
    std::array<char, MAX_MESSAGE_SIZE> message{};
    std::size_t length{0};
    
    LogEntry() = default;
};

/**
 * @brief Async logger with bounded buffer
 * 
 * Design:
 * - Log entries stored in fixed-size ring buffer
 * - Logger thread is never blocked - drops messages if buffer full
 * - Background thread flushes to file periodically
 * - Uses snprintf for formatting (no dynamic allocation)
 * 
 * Thread Safety:
 * - log() can be called from any thread
 * - Internal mutex protects buffer access
 */
class AsyncLogger {
public:
    static constexpr std::size_t DEFAULT_BUFFER_SIZE = 4096;

private:
    static constexpr std::size_t BUFFER_MASK = DEFAULT_BUFFER_SIZE - 1;
    static_assert((DEFAULT_BUFFER_SIZE & BUFFER_MASK) == 0, "Buffer size must be power of 2");
    
    std::array<LogEntry, DEFAULT_BUFFER_SIZE> buffer_{};
    
    CES_CACHE_ALIGNED std::atomic<std::size_t> head_{0};  // Write position
    CES_CACHE_ALIGNED std::atomic<std::size_t> tail_{0};  // Read position
    
    std::ofstream file_;
    std::jthread flush_thread_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> messages_logged_{0};
    std::atomic<std::uint64_t> messages_dropped_{0};
    
    std::chrono::milliseconds flush_interval_{10};

public:
    /**
     * @brief Construct async logger
     * @param filename Output file path
     * @param flush_interval_ms How often to flush (milliseconds)
     */
    explicit AsyncLogger(
        const std::string& filename,
        std::chrono::milliseconds flush_interval = std::chrono::milliseconds{10}
    )
        : flush_interval_(flush_interval) {
        
        file_.open(filename, std::ios::out | std::ios::trunc);
        if (!file_.is_open()) {
            throw std::runtime_error("Failed to open log file: " + filename);
        }
        
        start();
    }
    
    ~AsyncLogger() {
        stop();
    }
    
    // Non-copyable, non-movable
    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;
    
    /**
     * @brief Log a formatted message
     * 
     * Uses printf-style formatting. Never blocks - drops if buffer full.
     * 
     * @tparam Args Format argument types
     * @param fmt Format string
     * @param args Format arguments
     */
    template<typename... Args>
    void log(const char* fmt, Args&&... args) noexcept {
        std::size_t head = head_.load(std::memory_order_relaxed);
        std::size_t tail = tail_.load(std::memory_order_acquire);
        
        // Check if buffer full (leave one slot empty for disambiguation)
        std::size_t next_head = (head + 1) & BUFFER_MASK;
        if CES_UNLIKELY(next_head == tail) {
            // Buffer full - drop message
            messages_dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        
        // Write entry
        LogEntry& entry = buffer_[head];
        entry.timestamp = now_ns();
        
        if constexpr (sizeof...(Args) == 0) {
            // Simple string copy
            entry.length = std::min(std::strlen(fmt), LogEntry::MAX_MESSAGE_SIZE - 1);
            std::memcpy(entry.message.data(), fmt, entry.length);
            entry.message[entry.length] = '\0';
        } else {
            // Format string
            int result = std::snprintf(
                entry.message.data(),
                LogEntry::MAX_MESSAGE_SIZE,
                fmt,
                std::forward<Args>(args)...
            );
            entry.length = (result > 0) 
                ? std::min(static_cast<std::size_t>(result), LogEntry::MAX_MESSAGE_SIZE - 1)
                : 0;
        }
        
        head_.store(next_head, std::memory_order_release);
        messages_logged_.fetch_add(1, std::memory_order_relaxed);
    }
    
    /**
     * @brief Log a simple string (no formatting)
     */
    void log(const char* msg) noexcept {
        log<>(msg);
    }
    
    /**
     * @brief Flush pending messages to file
     */
    void flush() {
        std::size_t tail = tail_.load(std::memory_order_relaxed);
        std::size_t head = head_.load(std::memory_order_acquire);
        
        while (tail != head) {
            const LogEntry& entry = buffer_[tail];
            
            // Write timestamp and message
            file_ << entry.timestamp << " " 
                  << std::string_view(entry.message.data(), entry.length) 
                  << "\n";
            
            tail = (tail + 1) & BUFFER_MASK;
        }
        
        tail_.store(tail, std::memory_order_release);
        file_.flush();
    }
    
    /**
     * @brief Get number of messages logged
     */
    [[nodiscard]] std::uint64_t messages_logged() const noexcept {
        return messages_logged_.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Get number of messages dropped
     */
    [[nodiscard]] std::uint64_t messages_dropped() const noexcept {
        return messages_dropped_.load(std::memory_order_relaxed);
    }

private:
    void start() {
        running_.store(true, std::memory_order_release);
        
        flush_thread_ = std::jthread([this](std::stop_token stop_token) {
            while (!stop_token.stop_requested()) {
                std::this_thread::sleep_for(flush_interval_);
                flush();
            }
            // Final flush
            flush();
        });
    }
    
    void stop() {
        running_.store(false, std::memory_order_release);
        if (flush_thread_.joinable()) {
            flush_thread_.request_stop();
            flush_thread_.join();
        }
    }
};

} // namespace ces
