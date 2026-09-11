/// @file api/kmx/aio/promise.hpp
/// @brief Promise types behind task<T> and task<void>: result storage, exception capture and suspension points.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/promise_base.hpp>

    #include <concepts>
    #include <coroutine>
    #include <exception>
    #include <utility>
    #include <variant>
#endif

namespace kmx::aio
{
    /// @brief Forward declaration of the lazy coroutine task wrapper.
    /// @tparam T The task result type.
    template <typename T>
    class [[nodiscard]] task;

    /// @brief Promise type for a task producing a value.
    /// @tparam T The type the coroutine returns.
    template <typename T>
    struct promise: promise_base
    {
        /// @brief Storage for either the return value or an empty state.
        std::variant<std::monostate, T> result_;

        /// @brief Creates the public task wrapper for this promise.
        /// @return A task bound to this coroutine frame.
        /// @throws std::bad_alloc if the coroutine frame cannot be allocated.
        /// @note Defined in task.hpp, once task is complete.
        [[nodiscard]] task<T> get_return_object() noexcept(false);

        /// @brief Suspends immediately before the coroutine body begins.
        /// @return A suspending awaiter.
        [[nodiscard]] std::suspend_always initial_suspend() const noexcept { return {}; }

        /// @brief Suspends at coroutine completion and transfers to the continuation.
        /// @return The final awaiter.
        [[nodiscard]] final_awaiter final_suspend() const noexcept { return {}; }

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

    /// @brief Promise type for a task producing no value.
    template <>
    struct promise<void>: promise_base
    {
        /// @brief Creates the public task wrapper for this promise.
        /// @return A task bound to this coroutine frame.
        /// @throws std::bad_alloc if the coroutine frame cannot be allocated.
        /// @note Defined in task.hpp, once task is complete.
        task<void> get_return_object() noexcept(false);

        /// @brief Suspends immediately before the coroutine body begins.
        /// @return A suspending awaiter.
        [[nodiscard]] std::suspend_always initial_suspend() const noexcept { return {}; }
        /// @brief Suspends at coroutine completion and transfers to the continuation.
        /// @return The final awaiter.
        [[nodiscard]] final_awaiter final_suspend() const noexcept { return {}; }

        /// @brief Records an unhandled exception raised by the coroutine body.
        void unhandled_exception() noexcept { exception_ = std::current_exception(); }

        /// @brief Completes a void-returning coroutine.
        void return_void() const noexcept {}
    };
}
