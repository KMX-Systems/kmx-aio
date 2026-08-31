/// @file aio/task.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

/// @brief std::coroutine_traits Allocator Requirement (PLAN MANDATE)
///
/// Per Plan.md, Section "Critical Architecture Review, Item 3":
/// "Custom Memory Allocators (kmx::aio::allocator): Mandate std::coroutine_traits
///  overrides to route coroutine frame allocations to a thread-local, lockless
///  fixed-size Slab Allocator."
///
/// IMPLEMENTATION:
///   C++ coroutines route ALL frame allocations through promise_type::operator new/delete.
///   By specializing these methods in promise_base (the common base for promise<T> and
///   promise<void>), we intercept every frame allocation for every task<T> instance.
///
/// ALLOCATION STRATEGY:
///   1. Attempt allocation from thread-local slab allocator (O(1), lockless, no malloc).
///   2. If slab is exhausted or frame size exceeds slab slot size, fall back to ::operator new.
///   3. On deallocation, detect ownership via slab_allocator::owns() and route accordingly.
///
/// THREAD-LOCAL LIFECYCLE:
///   - set_thread_allocator(ptr) should be called at executor startup (per core).
///   - get_thread_allocator() retrieves the active slab for the current thread.
///   - Frames are O(1) allocated/deallocated with zero fragmentation.
///
/// PERFORMANCE:
///   - Zero malloc overhead for typical coroutine frames (200-512 bytes).
///   - Deterministic latency (no memory fragmentation, no system calls on hot path).
///   - Scales to millions of concurrent coroutines per core (pre-allocated slab).
///
#include "kmx/aio/task.hpp"
#include "kmx/aio/allocator.hpp"
#include <cstddef>
#include <new>

namespace kmx::aio::detail
{
    /// @brief Bytes reserved ahead of every coroutine frame, holding the slab that owns it.
    /// @details A frame is allocated on whichever thread created the coroutine and destroyed on
    ///          whichever thread last resumed it, and for anything that crosses an executor boundary
    ///          those are different threads. Deciding where a frame goes back to by asking the *freeing*
    ///          thread what slab it has installed answers a different question, and answers it wrongly:
    ///          a frame from another thread's slab is not recognized, goes to ::operator delete, and
    ///          corrupts the heap. The frame therefore carries its origin with it - the slab it came
    ///          from, or null for the heap - and costs one pointer of padding to say so.
    /// @note Sized to the default new alignment rather than to a pointer, so the frame handed back to
    ///       the compiler is aligned exactly as ::operator new would have aligned it.
    inline constexpr std::size_t frame_header_size = alignof(std::max_align_t);
    static_assert(frame_header_size >= sizeof(slab_allocator*), "the frame header must hold the owning slab pointer");

    void* promise_base::operator new(const std::size_t size) noexcept(false)
    {
        // One thread-local lookup for both the slab and the counters: this runs for every coroutine
        // frame the library creates, and the accounting must not cost more than the allocation.
        auto& state = current_thread_state();
        const std::size_t total = size + frame_header_size;

        /// Attempt allocation from thread-local slab allocator.
        if (auto* const alloc = state.allocator)
            if (total <= alloc->slot_size())
                if (void* const base = alloc->allocate())
                {
                    *static_cast<slab_allocator**>(base) = alloc;
                    state.count_slab_allocation();
                    return static_cast<std::byte*>(base) + frame_header_size;
                }

        /// Fall back to standard allocation if slab is exhausted or frame is too large.
        state.count_heap_allocation();
        void* const base = ::operator new(total);
        *static_cast<slab_allocator**>(base) = nullptr;
        return static_cast<std::byte*>(base) + frame_header_size;
    }

    void promise_base::operator delete(void* ptr, std::size_t /*size*/) noexcept
    {
        auto* const base = static_cast<std::byte*>(ptr) - frame_header_size;
        auto* const alloc = *reinterpret_cast<slab_allocator**>(base);
        if (alloc == nullptr)
        {
            ::operator delete(static_cast<void*>(base));
            return;
        }

        // The slab's free list belongs to the thread that allocates from it. This is that thread when
        // the frame is ending where it began, which is the ordinary case and the cheap one; anything
        // else goes on the slab's remote list for its owner to collect.
        if (current_thread_state().allocator == alloc)
            alloc->deallocate(base);
        else
            alloc->deallocate_remote(base);
    }
} // namespace kmx::aio::detail
