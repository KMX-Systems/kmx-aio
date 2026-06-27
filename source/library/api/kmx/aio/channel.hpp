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
    /// @brief Backpressure thresholds for producer-side throttling.
    struct channel_backpressure_config
    {
        std::size_t low_watermark = 256u;
        std::size_t high_watermark = 512u;
    };

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
            set_backpressure(channel_backpressure_config {});
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
            const auto tail = tail_.load(std::memory_order_acquire);
            const auto next_head = (head + 1u) & mask_;

            // Full when the next write position equals the current read position
            if (next_head == tail)
                return false;

            const auto occ = (head - tail) & mask_;
            const auto low = low_watermark_.load(std::memory_order_acquire);
            const auto high = high_watermark_.load(std::memory_order_acquire);
            const bool current_throttled = throttled_.load(std::memory_order_acquire);

            const bool throttled_before_push = compute_throttled(occ, low, high, current_throttled);
            if (throttled_before_push != current_throttled)
                throttled_.store(throttled_before_push, std::memory_order_release);

            if (throttled_before_push)
                return false;

            storage_[head] = std::move(value);
            head_.store(next_head, std::memory_order_release);

            const bool throttled_after_push = compute_throttled(occ + 1u, low, high, throttled_before_push);
            if (throttled_after_push != throttled_before_push)
                throttled_.store(throttled_after_push, std::memory_order_release);

            return true;
        }

        /// @brief Attempts to dequeue an element (consumer side).
        /// @return The dequeued element, or std::nullopt if the channel is empty.
        [[nodiscard]] std::optional<T> try_pop() noexcept
        {
            const auto tail = tail_.load(std::memory_order_relaxed);
            const auto head = head_.load(std::memory_order_acquire);

            // Empty when read position equals the write position
            if (tail == head)
                return {};

            T value = std::move(storage_[tail]);
            const auto next_tail = (tail + 1u) & mask_;
            tail_.store(next_tail, std::memory_order_release);
            tail_.notify_all();

            const auto occ_after_pop = (head - next_tail) & mask_;
            const auto low = low_watermark_.load(std::memory_order_acquire);
            if (occ_after_pop <= low)
            {
                const bool was_throttled = throttled_.exchange(false, std::memory_order_acq_rel);
                if (was_throttled)
                    throttled_.notify_all();
            }

            return value;
        }

        /// @brief Blocks until the channel can accept at least one more element.
        /// @details Uses atomic wait/notify to avoid busy-spinning while the producer
        ///          is throttled or the ring is full.
        /// @note Snapshot invariants:
        ///       1) sendability and wait target are derived from the same head/tail snapshot,
        ///       2) if the snapshot shows room, return immediately,
        ///       3) otherwise wait on either throttled_ or tail_ using that same snapshot value.
        ///       This avoids missed wakeups where a consumer pop happens between independent checks.
        void wait_until_can_send() noexcept
        {
            while (true)
            {
                const auto head = head_.load(std::memory_order_acquire);
                const auto tail_snapshot = tail_.load(std::memory_order_acquire);
                const auto occ = (head - tail_snapshot) & mask_;
                const auto low = low_watermark_.load(std::memory_order_acquire);
                const auto high = high_watermark_.load(std::memory_order_acquire);
                const bool throttled = compute_throttled(occ, low, high, throttled_.load(std::memory_order_acquire));

                if (!throttled && (occ < usable_capacity()))
                    return;

                if (throttled)
                    throttled_.wait(true, std::memory_order_relaxed);
                else
                    tail_.wait(tail_snapshot, std::memory_order_relaxed);
            }
        }

        /// @brief Checks if the channel is currently empty (consumer perspective).
        /// @return true if no elements are available to dequeue.
        [[nodiscard]] bool empty() const noexcept { return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire); }

        /// @brief Sets producer throttling thresholds.
        /// @param cfg Backpressure low/high watermark configuration.
        void set_backpressure(const channel_backpressure_config& cfg) noexcept
        {
            const auto usable_slots = usable_capacity();

            std::size_t high = cfg.high_watermark;
            if (high == 0u)
                high = 1u;
            if (high > usable_slots)
                high = usable_slots;

            std::size_t low = cfg.low_watermark;
            if (low > high)
                low = high;

            low_watermark_.store(low, std::memory_order_release);
            high_watermark_.store(high, std::memory_order_release);

            const auto occ = occupancy();
            const bool current_throttled = throttled_.load(std::memory_order_acquire);
            const bool next_throttled = compute_throttled(occ, low, high, current_throttled);
            throttled_.store(next_throttled, std::memory_order_release);
            if (current_throttled && !next_throttled)
                throttled_.notify_all();
        }

        /// @brief Returns current producer credits before high watermark is reached.
        /// @details Credits are consumed by pushes and replenished by pops.
        [[nodiscard]] std::size_t producer_credits() const noexcept
        {
            const auto occ = occupancy();
            const auto high = high_watermark_.load(std::memory_order_acquire);
            return (occ >= high) ? 0u : (high - occ);
        }

        /// @brief Checks if producer can enqueue according to configured backpressure.
        [[nodiscard]] bool can_send() const noexcept
        {
            const auto occ = occupancy();
            const auto low = low_watermark_.load(std::memory_order_acquire);
            const auto high = high_watermark_.load(std::memory_order_acquire);
            const bool throttled = compute_throttled(occ, low, high, throttled_.load(std::memory_order_acquire));

            if (throttled)
                return false;

            return occ < usable_capacity();
        }

        /// @brief Returns current number of queued elements.
        [[nodiscard]] std::size_t occupancy() const noexcept
        {
            const auto head = head_.load(std::memory_order_acquire);
            const auto tail = tail_.load(std::memory_order_acquire);
            return (head - tail) & mask_;
        }

        /// @brief Returns the internal ring size (always a power of two).
        /// @return Total ring slots, including one sentinel slot used to distinguish full/empty.
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

        /// @brief Stable cache-line size constant (avoids ABI-unstable std::hardware_destructive_interference_size).
        static constexpr std::size_t cache_line_size = 64u;

        [[nodiscard]] static bool compute_throttled(const std::size_t occupancy, const std::size_t low, const std::size_t high,
                                                    const bool current) noexcept
        {
            if (occupancy <= low)
                return false;
            if (occupancy >= high)
                return true;
            return current;
        }

        [[nodiscard]] std::size_t usable_capacity() const noexcept { return capacity_ - 1u; }

        std::size_t capacity_;
        std::size_t mask_;
        std::vector<T> storage_;

        std::atomic<std::size_t> low_watermark_ {};
        std::atomic<std::size_t> high_watermark_ {};
        std::atomic<bool> throttled_ {};

        // Separated cache lines to prevent false sharing between producer and consumer
        alignas(cache_line_size) std::atomic<std::size_t> head_ {};
        alignas(cache_line_size) std::atomic<std::size_t> tail_ {};
    };

} // namespace kmx::aio
