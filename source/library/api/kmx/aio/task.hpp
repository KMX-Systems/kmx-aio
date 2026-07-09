/// @file aio/task.hpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <concepts>
    #include <coroutine>
    #include <exception>
    #include <stop_token>
    #include <type_traits>
    #include <utility>
    #include <variant>

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/logger.hpp>
#endif

namespace kmx::aio
{
    /// @brief Tag type used to request the coroutine stop token via `co_await`.
    struct get_stop_token_t
    {
    };

    /// @brief Custom awaitable token that resolves to the current coroutine's stop token.
    constexpr get_stop_token_t get_stop_token {};

    /// @brief Forward declaration of the lazy coroutine task wrapper.
    /// @tparam T The task result type.
    template <typename T>
    class [[nodiscard]] task;

    namespace detail
    {
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
        /// See kmx/aio/task.cpp for full implementation and kmx/aio/allocator.hpp
        /// for slab_allocator design.
        struct promise_base
        {
            /// @brief Continuation to resume when the coroutine reaches final suspend.
            std::coroutine_handle<> continuation_;
            /// @brief Stored exception captured from the coroutine body.
            std::exception_ptr exception_;
            /// @brief Stop source associated with the coroutine instance.
            std::stop_source stop_source_;

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
                bool await_ready() const noexcept { return false; }

                /// @brief Transfers execution to the stored continuation at final suspend.
                /// @tparam P The promise type deriving from `promise_base`.
                /// @param h The coroutine handle being finalized.
                /// @return The continuation handle or `std::noop_coroutine()`.
                template <typename P>
                    requires std::is_base_of_v<promise_base, P>
                std::coroutine_handle<> await_suspend(std::coroutine_handle<P> h) const noexcept
                {
                    if (h.promise().continuation_)
                        return h.promise().continuation_;

                    return std::noop_coroutine();
                }

                /// @brief Completes the final suspend transition.
                void await_resume() const noexcept {}
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
            auto await_transform(get_stop_token_t) noexcept
            {
                struct awaiter
                {
                    /// @brief The stop token to return.
                    std::stop_token token;
                    /// @brief Always ready because the token is already available.
                    /// @return `true`.
                    bool await_ready() const noexcept { return true; }
                    /// @brief No suspension is required for stop-token retrieval.
                    /// @param h The coroutine handle.
                    void await_suspend(std::coroutine_handle<>) const noexcept {}
                    /// @brief Returns the captured stop token.
                    /// @return The coroutine stop token.
                    std::stop_token await_resume() const noexcept { return token; }
                };
                return awaiter {stop_source_.get_token()};
            }
        };

        template <typename T>
        struct promise: promise_base
        {
            /// @brief Storage for either the return value or an empty state.
            std::variant<std::monostate, T> result_;

            /// @brief Creates the public task wrapper for this promise.
            /// @return A task bound to this coroutine frame.
            /// @throws std::bad_alloc if the coroutine frame cannot be allocated.
            [[nodiscard]] task<T> get_return_object() noexcept(false);

            /// @brief Suspends immediately before the coroutine body begins.
            /// @return A suspending awaiter.
            std::suspend_always initial_suspend() const noexcept { return {}; }

            /// @brief Suspends at coroutine completion and transfers to the continuation.
            /// @return The final awaiter.
            final_awaiter final_suspend() const noexcept { return {}; }

            /// @brief Records an unhandled exception raised by the coroutine body.
            void unhandled_exception() noexcept { exception_ = std::current_exception(); }

            /// @brief Stores a successful return value.
            /// @tparam U The value type forwarded into T.
            /// @param value The return value produced by the coroutine.
            /// @throws Any exception raised by result construction.
            template <typename U>
                requires std::convertible_to<U, T>
            void return_value(U&& value) noexcept(false)
            {
                result_.template emplace<1>(std::forward<U>(value));
            }
        };

        template <>
        struct promise<void>: promise_base
        {
            /// @brief Creates the public task wrapper for this promise.
            /// @return A task bound to this coroutine frame.
            /// @throws std::bad_alloc if the coroutine frame cannot be allocated.
            task<void> get_return_object() noexcept(false);

            /// @brief Suspends immediately before the coroutine body begins.
            /// @return A suspending awaiter.
            std::suspend_always initial_suspend() const noexcept { return {}; }
            /// @brief Suspends at coroutine completion and transfers to the continuation.
            /// @return The final awaiter.
            final_awaiter final_suspend() const noexcept { return {}; }

            /// @brief Records an unhandled exception raised by the coroutine body.
            void unhandled_exception() noexcept { exception_ = std::current_exception(); }

            /// @brief Completes a void-returning coroutine.
            void return_void() const noexcept {}
        };
    } // namespace detail

    /// @brief A lazy coroutine task.
    /// @tparam T The result type produced by the coroutine.
    template <typename T = void>
    class [[nodiscard]] task
    {
    public:
        /// @brief The promise type backing this task.
        using promise_type = detail::promise<T>;
        /// @brief The coroutine handle type used by this task.
        using handle_type = std::coroutine_handle<promise_type>;

        /// @brief Constructs an empty task.
        task() noexcept = default;

        /// @brief Adopts an existing coroutine handle.
        /// @param h The coroutine handle to wrap.
        explicit task(const handle_type h) noexcept: handle_(h) {}

        /// @brief Destroys the owned coroutine frame if present.
        ~task() noexcept
        {
            if (handle_)
                handle_.destroy();
        }

        /// @brief Non-copyable.
        task(const task&) = delete;
        /// @brief Non-copyable.
        task& operator=(const task&) = delete;

        /// @brief Moves ownership of the coroutine handle.
        /// @param other The task to steal from.
        task(task&& other) noexcept: handle_(std::exchange(other.handle_, nullptr)) {}

        /// @brief Moves ownership of the coroutine handle.
        /// @param other The task to steal from.
        /// @return This task with the new handle.
        task& operator=(task&& other) noexcept
        {
            if (this != &other)
            {
                if (handle_)
                    handle_.destroy();
                handle_ = std::exchange(other.handle_, nullptr);
            }

            return *this;
        }

        /// @brief Indicates whether the task can resume immediately.
        /// @return `true` if the task is complete or empty.
        bool await_ready() const noexcept { return !handle_ || handle_.done(); }

        /// @brief Registers the awaiting coroutine as the continuation.
        /// @param continuation The coroutine that will resume after this task finishes.
        /// @return The coroutine handle to resume for symmetric transfer.
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept
        {
            handle_.promise().continuation_ = continuation;
            return handle_;
        }

        /// @brief Resumes the task and retrieves the result.
        /// @return The task result for non-void tasks.
        /// @throws The exception stored in the promise if one occurred.
        /// @throws std::bad_variant_access if result is missing (unlikely).
        [[nodiscard]] T await_resume() const noexcept(false)
        {
            if (handle_.promise().exception_)
                std::rethrow_exception(handle_.promise().exception_);

            if constexpr (!std::is_void_v<T>)
                return std::get<1u>(std::move(handle_.promise().result_));
        }

    private:
        /// @brief The owned coroutine handle, if any.
        handle_type handle_;
    };

    namespace detail
    {
        template <typename T>
        task<T> promise<T>::get_return_object() noexcept(false)
        {
            return task<T>(std::coroutine_handle<promise<T>>::from_promise(*this));
        }

        inline task<void> promise<void>::get_return_object() noexcept(false)
        {
            return task<void>(std::coroutine_handle<promise<void>>::from_promise(*this));
        }
    }

} // namespace kmx::aio
