/// @file api/kmx/aio/promise_base.hpp
/// @brief Common base of the task promises: slab-routed frame allocation, final-suspend transfer and stop-token access.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <coroutine>
    #include <cstddef>
    #include <exception>
    #include <stop_token>
    #include <type_traits>
    #include <utility>
#endif

namespace kmx::aio
{
    /// @brief Tag type used to request the coroutine stop token via `co_await`.
    struct stop_token_t
    {
    };

    /// @brief Custom awaitable token that resolves to the current coroutine's stop token.
    constexpr stop_token_t get_stop_token {};

    /// @brief Type-erased handle to a suspended coroutine, whatever its promise type.
    /// @details What awaiters and schedulers pass around: enough to resume or destroy a coroutine,
    ///          without naming the frame it belongs to.
    using coroutine_handle_t = std::coroutine_handle<>;

    /// @brief Base for task promise types with std::coroutine_traits-aware allocator override.
    /// @details
    /// This struct implements operator new/delete to satisfy the Plan.md requirement:
    /// "Custom Memory Allocators: Mandate std::coroutine_traits overrides to route
    ///  coroutine frame allocations to a thread-local, lockless fixed-size Slab Allocator."
    ///
    /// When C++ coroutines create a frame for task<T>, the compiler calls
    /// promise_type::operator new (inherited from promise_base), which:
    ///   1. Attempts O(1) allocation from thread-local slab allocator.
    ///   2. Falls back to ::operator new if slab is exhausted or frame is oversized.
    ///
    /// See kmx/aio/promise_base.cpp for full implementation and kmx/aio/allocator/slab.hpp
    /// for allocator::slab design.
    struct promise_base
    {
        /// @brief Continuation to resume when the coroutine reaches final suspend.
        coroutine_handle_t continuation_ {};
        /// @brief Stored exception captured from the coroutine body.
        std::exception_ptr exception_ {};
        /// @brief Stop source associated with the coroutine instance.
        std::stop_source stop_source_;
        /// @brief Stop token handed down from whoever started or awaited this coroutine.
        /// @note Without this the per-coroutine stop_source_ above is unreachable from outside, so
        ///       co_await get_stop_token yields a token nothing can ever signal - cancellation that
        ///       compiles, type-checks and silently never fires. A token set here takes precedence, and is
        ///       inherited by every task this one awaits, so a single stop_source cancels a whole chain.
        std::stop_token stop_token_ {};
        bool has_external_stop_token_ {};

        promise_base() noexcept = default;

        promise_base(const promise_base&) = delete;
        promise_base& operator=(const promise_base&) = delete;
        promise_base(promise_base&&) = delete;
        promise_base& operator=(promise_base&&) = delete;

        /// @brief Allocates coroutine frame storage.
        /// @param size The frame size requested by the compiler.
        /// @return A pointer to frame storage.
        /// @throws std::bad_alloc if allocation fails.
        static void* operator new(const std::size_t size) noexcept(false);
        /// @brief Releases coroutine frame storage.
        /// @param ptr The frame storage pointer to free.
        /// @param size The frame size originally requested.
        static void operator delete(void* ptr, std::size_t /*size*/) noexcept;

        /// @brief Final suspension point that transfers control to the continuation.
        struct final_awaiter
        {
            /// @brief Indicates whether final suspension is immediate.
            /// @return Always `false` to force suspension semantics.
            [[nodiscard]] bool await_ready() const noexcept { return false; }

            /// @brief Transfers execution to the stored continuation at final suspend.
            /// @tparam P The promise type deriving from `promise_base`.
            /// @param h The coroutine handle being finalized.
            /// @return The continuation handle or `std::noop_coroutine()`.
            template <typename P>
                requires std::is_base_of_v<promise_base, P>
            [[nodiscard]] coroutine_handle_t await_suspend(std::coroutine_handle<P> h) const noexcept
            {
                if (h.promise().continuation_) // LCOV_EXCL_BR_LINE: see below
                    return h.promise().continuation_;

                // LCOV_EXCL_START
                // A task reaches its final suspend with no continuation only if nobody awaited it.
                // Every task the library runs is spawned, and spawn() hands it to execute_task,
                // which awaits it - so the continuation is always set by the time this runs. There
                // is no public way to start a task without awaiting it, and the fallback stays for
                // the day one appears.
                return std::noop_coroutine();
            }

            /// @brief Completes the final suspend transition.
            void await_resume() const noexcept {}
            // LCOV_EXCL_STOP
        };

        /// @brief Forwards ordinary awaitables through the task promise.
        /// @tparam U The awaitable type.
        /// @param awaitable The awaitable object.
        /// @return The forwarded awaitable.
        template <typename U>
        decltype(auto) await_transform(U&& awaitable) noexcept
        {
            return std::forward<U>(awaitable);
        }

        /// @brief Special await transform that yields the coroutine stop token.
        /// @param get_stop_token The stop-token tag.
        /// @return An awaiter that resolves to the coroutine's stop token.
        auto await_transform(stop_token_t) noexcept
        {
            struct awaiter
            {
                /// @brief The stop token to return.
                std::stop_token token;
                /// @brief Always ready because the token is already available.
                /// @return `true`.
                [[nodiscard]] bool await_ready() const noexcept { return true; }
                /// @brief No suspension is required for stop-token retrieval.
                /// @param h The coroutine handle.
                /// @note Never called: await_ready() above returns true, so the coroutine takes the
                ///       token without suspending. It exists because the awaiter concept asks for
                ///       it.
                void await_suspend(coroutine_handle_t) const noexcept {} // LCOV_EXCL_LINE
                /// @brief Returns the captured stop token.
                /// @return The coroutine stop token.
                [[nodiscard]] std::stop_token await_resume() const noexcept { return token; }
            };
            // Whatever was handed down, and nothing if nothing was. Falling back to this coroutine's own
            // stop_source_ would be worse than useless: no one outside can reach it, so the token would
            // report stop_possible() == true and stop_requested() == false forever. Code that checks
            // whether cancellation is available would conclude that it is, and then wait for a signal that
            // cannot arrive. An empty token says plainly that nobody wired one up.
            return awaiter {stop_token_};
        }
    };
}
