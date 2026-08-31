/// @file aio/allocator.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/allocator.hpp"

#include <mutex>
#include <new>

namespace kmx::aio
{
    namespace detail
    {
        /// @brief Guards the registry of per-thread blocks.
        /// @note Taken when a thread allocates its first coroutine frame, and when the statistics are
        ///       read. Never on the allocation path itself.
        std::mutex g_statistics_mutex {};

        /// @brief Head of the intrusive list of per-thread blocks.
        thread_state* g_thread_states {};

        /// @brief The calling thread's block, or null until it allocates its first coroutine frame.
        /// @note A plain pointer, so reaching it costs a thread-local load and nothing else - no
        ///       initialization guard, no destructor, and so nothing that could run before or after
        ///       another thread-local whose own teardown frees a coroutine frame.
        thread_local thread_state* t_thread_state = nullptr;

        /// @brief Creates and registers this thread's block.
        /// @return The new block, which lives for the rest of the process.
        [[nodiscard]] static thread_state* create_thread_state() noexcept
        {
            auto* const state = new (std::nothrow) thread_state {};
            if (state == nullptr)
            {
                // Nothing here can report a failure - this runs inside a coroutine frame allocation
                // that has its own way of failing - and the counters are diagnostics. A shared block
                // keeps the caller running; what it costs is that the counts of a thread that could
                // not get its own block are mixed in with it.
                static thread_state fallback {};
                return &fallback;
            }

            const std::lock_guard lock(g_statistics_mutex);
            state->next = g_thread_states;
            g_thread_states = state;
            return state;
        }

        thread_state& current_thread_state() noexcept
        {
            if (t_thread_state == nullptr) [[unlikely]]
                t_thread_state = create_thread_state();

            return *t_thread_state;
        }
    } // namespace detail

    allocator_statistics g_allocator_statistics {};

    void set_thread_allocator(slab_allocator* alloc) noexcept
    {
        detail::current_thread_state().allocator = alloc;
    }

    slab_allocator* get_thread_allocator() noexcept
    {
        return detail::current_thread_state().allocator;
    }

    std::uint64_t allocation_counter::load(std::memory_order) const noexcept
    {
        std::uint64_t total {};
        const std::lock_guard lock(detail::g_statistics_mutex);
        for (const auto* state = detail::g_thread_states; state != nullptr; state = state->next)
            total += (kind_ == detail::counter_kind::slab) ? state->slab_allocations.load(std::memory_order_relaxed) :
                                                             state->heap_allocations.load(std::memory_order_relaxed);

        return total;
    }

    void allocator_statistics::reset() noexcept
    {
        const std::lock_guard lock(detail::g_statistics_mutex);
        for (auto* state = detail::g_thread_states; state != nullptr; state = state->next)
        {
            state->slab_allocations.store(0u, std::memory_order_relaxed);
            state->heap_allocations.store(0u, std::memory_order_relaxed);
        }
    }

    allocator_statistics& get_allocator_statistics() noexcept
    {
        return g_allocator_statistics;
    }
} // namespace kmx::aio
