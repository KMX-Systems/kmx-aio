/// @file aio/channel.hpp
/// @brief Lock-free Single-Producer Single-Consumer (SPSC) channel for cross-thread dispatch.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <memory>
    #include <optional>
    #include <type_traits>
    #include <utility>

    #include <kmx/aio/basic_channel.hpp>
#endif

namespace kmx::aio
{
    /// @brief A bounded, lock-free, cache-friendly SPSC ring buffer.
    /// @details Designed for zero-contention inter-thread communication between
    ///          executor threads (e.g. an io_uring market-data thread dispatching
    ///          orders to a strategy thread). Capacity is rounded up to the next
    ///          power of two for branchless index masking. All index and backpressure
    ///          handling lives in basic_channel; this class adds only the typed storage.
    /// @tparam T The element type. Must be nothrow move-constructible.
    /// @note Only occupied slots hold a live T. The ring is raw storage the channel constructs into on
    ///       push and destroys on pop, so T needs neither a default constructor nor move assignment,
    ///       and a queue of 4096 slots costs 4096 constructions over its whole life rather than at
    ///       construction. What that buys is worth stating: an element carrying a buffer lease or a
    ///       socket - anything whose default state is a lie the code then has to guard against - can be
    ///       sent through the channel as-is.
    template <typename T>
        requires std::is_nothrow_move_constructible_v<T>
    class channel: public basic_channel
    {
    public:
        /// @brief Constructs a channel with the given minimum capacity.
        /// @param min_capacity Minimum number of slots. Rounded up to next power of two.
        /// @throws std::bad_alloc if the backing storage cannot be allocated.
        explicit channel(const std::size_t min_capacity) noexcept(false):
            basic_channel(min_capacity),
            storage_(std::make_unique<element_slot[]>(capacity()))
        {
        }

        /// @brief Non-copyable.
        channel(const channel&) = delete;
        /// @brief Non-copyable.
        channel& operator=(const channel&) = delete;

        /// @brief Non-movable, for the reason basic_channel is: a channel in use is held by a producer
        ///        and a consumer at once, and moving the ring out from under either is a data race.
        channel(channel&&) = delete;
        /// @brief Non-movable.
        channel& operator=(channel&&) = delete;

        /// @brief Destroys the channel and every element still queued in it.
        ~channel() noexcept
        {
            pop_slot slot {};
            while (acquire_pop_slot(slot))
            {
                std::destroy_at(cell(slot.index));
                publish_pop(slot);
            }
        }

        /// @brief Attempts to enqueue an element (producer side).
        /// @param value The value to enqueue via move.
        /// @return true if the element was enqueued, false if the channel is full.
        [[nodiscard]] bool try_push(T&& value) noexcept
        {
            push_slot slot {};
            if (!acquire_push_slot(slot))
                return false;

            // The slot is reserved but not yet published, so the consumer cannot reach it: the element
            // is built in place first and only then made visible.
            std::construct_at(cell(slot.index), std::move(value));
            publish_push(slot);
            return true;
        }

        /// @brief Attempts to dequeue an element (consumer side).
        /// @return The dequeued element, or std::nullopt if the channel is empty.
        [[nodiscard]] std::optional<T> try_pop() noexcept
        {
            pop_slot slot {};
            if (!acquire_pop_slot(slot))
                return {};

            T* const source = cell(slot.index);
            std::optional<T> value(std::move(*source));

            // The slot must be freed of its element before it is published: publishing hands it back to
            // the producer, which constructs into it.
            std::destroy_at(source);
            publish_pop(slot);
            return value;
        }

    private:
        /// @brief One ring slot: storage for a T whose lifetime the channel drives by hand.
        /// @details A union rather than a byte array so the storage carries T's size and alignment
        ///          without a cast, and so reading @c value after construct_at() needs no std::launder.
        ///          Both special members are user-provided and empty: creating the ring must not
        ///          construct elements, and destroying it must not destroy slots the channel already
        ///          emptied.
        union element_slot
        {
            /// @brief Leaves the slot empty; no T is constructed.
            element_slot() noexcept {}
            /// @brief Non-copyable: a slot's occupancy is known only to the ring indices.
            element_slot(const element_slot&) = delete;
            /// @brief Non-copyable.
            element_slot& operator=(const element_slot&) = delete;
            /// @brief Destroys nothing; the channel destroys live elements itself.
            ~element_slot() noexcept {}

            /// @brief The element, alive only while its slot sits between the tail and the head.
            T value;
        };

        /// @brief Returns the address of the element cell at @p index.
        /// @param index A ring index handed out by acquire_push_slot() or acquire_pop_slot().
        [[nodiscard]] T* cell(const std::size_t index) noexcept { return std::addressof(storage_[index].value); }

        // Own cache line: the producer reads this pointer on every push, and it must not
        // share a line with the consumer-written tail index at the end of the base.
        /// @brief The element storage backing the ring; sized to the base class's slot count.
        alignas(cache_line_size) std::unique_ptr<element_slot[]> storage_;
    };

} // namespace kmx::aio
