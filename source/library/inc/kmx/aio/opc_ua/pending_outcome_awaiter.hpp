/// @file aio/opc_ua/pending_outcome_awaiter.hpp
/// @brief Awaiter that suspends a coroutine until a pending request state carries its outcome.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <expected>
    #include <memory>
    #include <system_error>
    #include <utility>

    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::opc_ua::detail
{
    /// @brief Suspends the awaiting coroutine until a request state holds its outcome.
    /// @details The awaiter never resumes anyone itself: it parks the awaiting coroutine in
    ///          `State::continuation`, and whoever fills `State::outcome` - a backend completion
    ///          callback or a bulk failure sweep - hands that handle back to the scheduler.
    /// @tparam State  Request state exposing `outcome` (an optional expected) and `continuation`.
    /// @tparam Result The value type carried by a successful outcome.
    template <typename State, typename Result>
    class pending_outcome_awaiter
    {
    public:
        /// @brief Bind the awaiter to the request state to observe.
        /// @param state Shared request state kept alive for as long as the awaiter exists.
        explicit pending_outcome_awaiter(std::shared_ptr<State> state) noexcept: state_(std::move(state)) {}

        /// @brief Check whether the outcome already arrived before the coroutine suspended.
        /// @return `true` when the request completed synchronously.
        [[nodiscard]] bool await_ready() const noexcept { return state_->outcome.has_value(); }

        /// @brief Record the awaiting coroutine as the request's continuation.
        /// @param continuation The coroutine to resume once the outcome is stored.
        /// @return Always `true`: the coroutine stays suspended until the request completes.
        bool await_suspend(coroutine_handle_t continuation) noexcept
        {
            state_->continuation = continuation;
            return true;
        }

        /// @brief Take the stored outcome.
        /// @return The request result or the error that ended it.
        [[nodiscard]] std::expected<Result, std::error_code> await_resume() noexcept { return std::move(*state_->outcome); }

    private:
        /// @brief The request state holding the outcome and the parked continuation.
        std::shared_ptr<State> state_;
    };

} // namespace kmx::aio::opc_ua::detail
