/// @file aio/basic_channel.hpp
/// @brief Element-type independent core of the SPSC channel: ring indices and backpressure.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <atomic>
    #include <cstddef>
#endif

namespace kmx::aio
{
    /// @brief Backpressure thresholds for producer-side throttling.
    struct channel_backpressure_config
    {
        std::size_t low_watermark = 256u;
        std::size_t high_watermark = 512u;
    };

    /// @brief Element-type independent part of an SPSC ring buffer.
    /// @details Owns the ring indices, the watermark configuration and the throttling state,
    ///          i.e. everything that does not depend on the element type. The typed derived
    ///          class only adds the storage and the element move in/out. The slot protocol is
    ///          acquire-then-publish: acquire_push_slot()/publish_push() on the producer side
    ///          and acquire_pop_slot()/publish_pop() on the consumer side.
    /// @note Not polymorphic: the destructor is protected and non-virtual.
    class basic_channel
    {
    public:
        /// @brief Blocks until the channel can accept at least one more element.
        /// @details Uses atomic wait/notify to avoid busy-spinning while the producer
        ///          is throttled or the ring is full.
        /// @note Snapshot invariants:
        ///       1) sendability and wait target are derived from the same head/tail snapshot,
        ///       2) if the snapshot shows room, return immediately,
        ///       3) otherwise wait on either throttled_ or tail_ using that same snapshot value.
        ///       This avoids missed wakeups where a consumer pop happens between independent checks.
        void wait_until_can_send() noexcept;

        /// @brief Checks if the channel is currently empty (consumer perspective).
        /// @return true if no elements are available to dequeue.
        [[nodiscard]] bool empty() const noexcept { return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire); }

        /// @brief Sets producer throttling thresholds.
        /// @param cfg Backpressure low/high watermark configuration.
        void set_backpressure(const channel_backpressure_config& cfg) noexcept;

        /// @brief Returns current producer credits before high watermark is reached.
        /// @details Credits are consumed by pushes and replenished by pops.
        [[nodiscard]] std::size_t producer_credits() const noexcept;

        /// @brief Checks if producer can enqueue according to configured backpressure.
        [[nodiscard]] bool can_send() const noexcept;

        /// @brief Returns current number of queued elements.
        [[nodiscard]] std::size_t occupancy() const noexcept;

        /// @brief Returns the internal ring size (always a power of two).
        /// @return Total ring slots, including one sentinel slot used to distinguish full/empty.
        [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    protected:
        /// @brief A write slot acquired by the producer, plus the state its publication depends on.
        struct push_slot
        {
            std::size_t index {};     ///< Ring index the element must be written to.
            std::size_t occupancy {}; ///< Occupancy observed before the write.
            std::size_t low {};       ///< Low watermark observed before the write.
            std::size_t high {};      ///< High watermark observed before the write.
        };

        /// @brief A read slot acquired by the consumer, plus the state its publication depends on.
        struct pop_slot
        {
            std::size_t index {}; ///< Ring index the element must be read from.
            std::size_t head {};  ///< Write position observed before the read.
        };

        /// @brief Constructs the ring state with the given minimum capacity.
        /// @param min_capacity Minimum number of slots. Rounded up to next power of two.
        explicit basic_channel(const std::size_t min_capacity) noexcept: capacity_(next_power_of_two(min_capacity)), mask_(capacity_ - 1u)
        {
            set_backpressure(channel_backpressure_config {});
        }

        /// @brief Non-copyable.
        basic_channel(const basic_channel&) = delete;
        /// @brief Non-copyable.
        basic_channel& operator=(const basic_channel&) = delete;

        /// @brief Non-movable: the ring indices are atomics a second thread reads concurrently, and a
        ///        channel in use is held by a producer and a consumer at once. Defaulting these made
        ///        them implicitly deleted anyway; saying so is what stops a caller reading the
        ///        declaration as an offer.
        basic_channel(basic_channel&&) = delete;
        /// @brief Non-movable.
        basic_channel& operator=(basic_channel&&) = delete;

        ~basic_channel() noexcept = default;

        /// @brief Reserves the next write slot if the ring has room and the producer is not throttled.
        /// @param slot Receives the slot description on success; untouched otherwise.
        /// @return true if a slot was reserved and must be published with publish_push().
        [[nodiscard]] bool acquire_push_slot(push_slot& slot) noexcept;

        /// @brief Publishes a slot reserved by acquire_push_slot() once the element has been written.
        /// @param slot The slot returned by the matching acquire_push_slot() call.
        void publish_push(const push_slot& slot) noexcept;

        /// @brief Reserves the next read slot if the ring is not empty.
        /// @param slot Receives the slot description on success; untouched otherwise.
        /// @return true if a slot was reserved and must be published with publish_pop().
        [[nodiscard]] bool acquire_pop_slot(pop_slot& slot) noexcept;

        /// @brief Publishes a slot reserved by acquire_pop_slot() once the element has been moved out.
        /// @param slot The slot returned by the matching acquire_pop_slot() call.
        void publish_pop(const pop_slot& slot) noexcept;

        /// @brief Stable cache-line size constant (avoids ABI-unstable std::hardware_destructive_interference_size).
        static constexpr std::size_t cache_line_size = 64u;

    private:
        /// @brief The ring indices and watermarks, all of which are shared between the two threads.
        using atomic_size_t = std::atomic<std::size_t>;

        /// @brief Rounds n up to the next power of two. Returns at least 2.
        [[nodiscard]] static std::size_t next_power_of_two(std::size_t n) noexcept;

        /// @brief Applies the watermark hysteresis to an occupancy.
        /// @param occupancy Number of queued elements observed.
        /// @param low Low watermark: at or below it the producer is released.
        /// @param high High watermark: at or above it the producer is throttled.
        /// @param current The throttling state in effect, kept while occupancy sits between the marks.
        [[nodiscard]] static bool compute_throttled(std::size_t occupancy, std::size_t low, std::size_t high, bool current) noexcept;

        /// @brief Returns the number of slots usable for elements (the ring less its sentinel slot).
        [[nodiscard]] std::size_t usable_capacity() const noexcept { return capacity_ - 1u; }

        std::size_t capacity_;
        std::size_t mask_;

        atomic_size_t low_watermark_ {};
        atomic_size_t high_watermark_ {};
        std::atomic_bool throttled_ {};

        // Separated cache lines to prevent false sharing between producer and consumer
        alignas(cache_line_size) atomic_size_t head_ {};
        alignas(cache_line_size) atomic_size_t tail_ {};
    };

} // namespace kmx::aio
