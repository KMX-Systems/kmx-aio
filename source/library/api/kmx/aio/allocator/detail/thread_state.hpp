/// @file aio/allocator/detail/thread_state.hpp
/// @brief Per-thread block holding a thread's slab and its allocation counters.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/allocator/detail/counter_kind.hpp>

    #include <atomic>
    #include <cstdint>
#endif

namespace kmx::aio::allocator
{
    class slab;

    namespace detail
    {
        /// @brief Everything the coroutine-frame allocator keeps per thread.
        /// @details One thread-local block holds both the slab this thread allocates frames from and
        ///          the counters saying where those frames came from, so the allocation path performs a
        ///          single thread-local lookup rather than one per item.
        /// @note The counters are atomic only so that the statistics may be read from another thread
        ///       without a data race. They are written exclusively by their owning thread, and with a
        ///       plain relaxed load-add-store rather than a read-modify-write: a locked increment of a
        ///       process-wide counter costs more than the slab allocation it is counting, and puts
        ///       every executor thread on the same cache line to pay for it.
        /// @note A block is never destroyed. It is small, and outliving its thread is what lets the
        ///       process-wide totals stay correct - and readable - after that thread has exited,
        ///       without any teardown ordering to get wrong on a path that runs for every coroutine
        ///       frame. One block is kept per thread that has allocated a frame.
        struct thread_state
        {
            /// @brief Counts one coroutine frame served from the slab.
            void count_slab_allocation() noexcept
            {
                slab_allocations.store(slab_allocations.load(std::memory_order_relaxed) + 1u, std::memory_order_relaxed);
            }

            /// @brief Counts one coroutine frame served from the heap.
            void count_heap_allocation() noexcept
            {
                heap_allocations.store(heap_allocations.load(std::memory_order_relaxed) + 1u, std::memory_order_relaxed);
            }

            /// @brief The slab this thread allocates coroutine frames from, if one was installed.
            slab* allocator {};
            /// @brief Frames this thread took from its slab.
            std::atomic_uint64_t slab_allocations {};
            /// @brief Frames this thread took from the heap.
            std::atomic_uint64_t heap_allocations {};
            /// @brief Next block in the process-wide registry.
            thread_state* next {};
        };

        /// @brief Returns the calling thread's block, creating and registering it on first use.
        [[nodiscard]] thread_state& current_thread_state() noexcept;

        /// @brief Sums one per-thread counter across every thread that has allocated a frame.
        /// @param kind Which counter to sum.
        /// @return The process-wide total.
        /// @note The registry lock lives entirely in this component; nothing else touches the list.
        [[nodiscard]] std::uint64_t total_allocations(const counter_kind kind) noexcept;

        /// @brief Zeroes both counters on every registered thread.
        void reset_allocations() noexcept;
    } // namespace detail

} // namespace kmx::aio::allocator
