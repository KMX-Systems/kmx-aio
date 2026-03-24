/// @file aio/channel.hpp
/// @brief Lock-free Single-Producer Single-Consumer (SPSC) channel for cross-thread dispatch.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <atomic>
    #include <cstddef>
    #include <new>
    #include <optional>
    #include <type_traits>
    #include <utility>
    #include <vector>
#endif

namespace kmx::aio
{
    /// @brief A bounded, lock-free, cache-friendly SPSC ring buffer.
    /// @details Designed for zero-contention inter-thread communication between
    ///          executor threads (e.g. an io_uring market-data thread dispatching
    ///          orders to a strategy thread). Capacity is rounded up to the next
    ///          power of two for branchless index masking.
    /// @tparam T The element type. Must be nothrow move-constructible.
    template <typename T>
        requires std::is_nothrow_move_constructible_v<T>
    class channel
    {
    public:
        /// @brief Constructs a channel with the given minimum capacity.
        /// @param min_capacity Minimum number of slots. Rounded up to next power of two.
        /// @throws std::bad_alloc if the backing storage cannot be allocated.
        explicit channel(const std::size_t min_capacity) noexcept(false):
            capacity_(next_power_of_two(min_capacity)),
            mask_(capacity_ - 1u),
            storage_(capacity_)
        {
        }

        /// @brief Non-copyable.
        channel(const channel&) = delete;
        /// @brief Non-copyable.
        channel& operator=(const channel&) = delete;

        /// @brief Move constructor.
        channel(channel&&) noexcept = default;
        /// @brief Move assignment.
        channel& operator=(channel&&) noexcept = default;

        ~channel() noexcept = default;

        /// @brief Attempts to enqueue an element (producer side).
        /// @param value The value to enqueue via move.
        /// @return true if the element was enqueued, false if the channel is full.
        [[nodiscard]] bool try_push(T&& value) noexcept
        {
            const auto head = head_.load(std::memory_order_relaxed);
            const auto next_head = (head + 1u) & mask_;

            // Full when the next write position equals the current read position
            if (next_head == tail_.load(std::memory_order_acquire))
                return false;

            storage_[head] = std::move(value);
            head_.store(next_head, std::memory_order_release);
            return true;
        }

        /// @brief Attempts to dequeue an element (consumer side).
        /// @return The dequeued element, or std::nullopt if the channel is empty.
        [[nodiscard]] std::optional<T> try_pop() noexcept
        {
            const auto tail = tail_.load(std::memory_order_relaxed);

            // Empty when read position equals the write position
            if (tail == head_.load(std::memory_order_acquire))
                return {};

            T value = std::move(storage_[tail]);
            tail_.store((tail + 1u) & mask_, std::memory_order_release);
            return value;
        }

        /// @brief Checks if the channel is currently empty (consumer perspective).
        /// @return true if no elements are available to dequeue.
        [[nodiscard]] bool empty() const noexcept { return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire); }

        /// @brief Returns the fixed capacity of the channel (always a power of two).
        /// @return The number of usable slots.
        [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    private:
        /// @brief Rounds n up to the next power of two. Returns at least 2.
        [[nodiscard]] static constexpr std::size_t next_power_of_two(std::size_t n) noexcept
        {
            if (n < 2u)
                return 2u;

            --n;
            n |= n >> 1u;
            n |= n >> 2u;
            n |= n >> 4u;
            n |= n >> 8u;
            n |= n >> 16u;
            n |= n >> 32u;
            return n + 1u;
        }

        std::size_t capacity_;
        std::size_t mask_;
        std::vector<T> storage_;

        // Separated cache lines to prevent false sharing between producer and consumer
        alignas(std::hardware_destructive_interference_size) std::atomic<std::size_t> head_ {};
        alignas(std::hardware_destructive_interference_size) std::atomic<std::size_t> tail_ {};
    };

} // namespace kmx::aio
