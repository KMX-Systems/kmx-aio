/// @file inc/kmx/aio/detail/scope_exit.hpp
/// @brief Scope guard that runs a callable when the scope owning it ends, on every exit path.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <type_traits>
    #include <utility>
#endif

namespace kmx::aio::detail
{
    /// @brief Calls a function when it goes out of scope.
    /// @tparam Callable A callable taking no arguments that does not throw.
    /// @details For the clean-up a function owes on every way out - a return, a coroutine's co_return, an
    ///          exception - without declaring a one-off class in the function body for it. The guard can be
    ///          neither copied nor moved, so the call happens exactly once, where the guard was declared.
    template <typename Callable>
        requires std::is_nothrow_invocable_v<Callable&>
    class scope_exit
    {
    public:
        /// @brief Takes the callable to run at the end of the scope.
        /// @param callable What to call on destruction.
        explicit scope_exit(Callable callable) noexcept(std::is_nothrow_move_constructible_v<Callable>): callable_(std::move(callable)) {}

        /// @brief Runs the callable.
        ~scope_exit() noexcept { callable_(); }

        scope_exit(const scope_exit&) = delete;
        scope_exit& operator=(const scope_exit&) = delete;
        scope_exit(scope_exit&&) = delete;
        scope_exit& operator=(scope_exit&&) = delete;

    private:
        /// @brief What to call on destruction.
        Callable callable_;
    };
}
