/// @file aio/channel.hpp
/// @brief Lock-free Single-Producer Single-Consumer (SPSC) channel for cross-thread dispatch.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <optional>
    #include <type_traits>
    #include <utility>
    #include <vector>

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
    template <typename T>
        requires std::is_nothrow_move_constructible_v<T>
    class channel: public basic_channel
    {
    public:
        /// @brief Constructs a channel with the given minimum capacity.
        /// @param min_capacity Minimum number of slots. Rounded up to next power of two.
        /// @throws std::bad_alloc if the backing storage cannot be allocated.
        explicit channel(const std::size_t min_capacity) noexcept(false): basic_channel(min_capacity), storage_(capacity()) {}

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
            push_slot slot {};
            if (!acquire_push_slot(slot))
                return false;

            storage_[slot.index] = std::move(value);
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

            T value = std::move(storage_[slot.index]);
            publish_pop(slot);
            return value;
        }

    private:
        // Own cache line: the producer reads these pointers on every push, and they must not
        // share a line with the consumer-written tail index at the end of the base.
        alignas(cache_line_size) std::vector<T> storage_;
    };

} // namespace kmx::aio
