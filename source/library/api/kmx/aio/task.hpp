/// @file api/kmx/aio/task.hpp
/// @brief Lazy coroutine task type, its stop-token propagation, and the task aliases for expected results.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/promise.hpp>
    #include <kmx/aio/promise_base.hpp>
    #include <kmx/logger.hpp>

    #include <coroutine>
    #include <exception>
    #include <source_location>
    #include <stop_token>
    #include <type_traits>
    #include <utility>
    #include <variant>
#endif

namespace kmx::aio
{
    /// @brief A lazy coroutine task.
    /// @tparam T The result type produced by the coroutine.
    template <typename T = void>
    class [[nodiscard]] task
    {
    public:
        /// @brief The promise type backing this task.
        using promise_type = promise<T>;
        /// @brief The coroutine handle type used by this task.
        using handle_type = std::coroutine_handle<promise_type>;

        /// @brief Constructs an empty task.
        task() noexcept = default;

        /// @brief Adopts an existing coroutine handle.
        /// @param h The coroutine handle to wrap.
        explicit task(const handle_type h) noexcept: handle_(h) {}

        /// @brief Destroys the owned coroutine frame if present.
        /// @note A coroutine that ends by throwing parks the exception in its promise, where it stays
        ///       until an awaiting coroutine takes it out in await_resume(). A task destroyed with one
        ///       still in place was therefore never awaited, and the failure would go down with the
        ///       frame without a trace - the shape of bug where an operation simply never happens and
        ///       nothing anywhere says why. It is reported here instead.
        ~task() noexcept
        {
            if (handle_)
            {
                static_cast<void>(request_stop());
                report_unretrieved_exception();
                handle_.destroy();
            }
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
        /// @note The empty and already-finished arms are guards, not paths: awaiting a moved-from task
        ///       or one that has already completed is undefined through this API, so no test can take
        ///       them without writing the undefined behaviour it would be testing.
        [[nodiscard]] bool await_ready() const noexcept { return !handle_ || handle_.done(); } // LCOV_EXCL_BR_LINE

        /// @brief Indicates whether this wrapper owns a coroutine frame.
        [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(handle_); }

        /// @brief Indicates whether the owned coroutine has reached final suspend.
        [[nodiscard]] bool done() const noexcept { return !handle_ || handle_.done(); }

        /// @brief Requests cancellation through the task's internal stop source.
        /// @return `true` when this call newly requested stop, otherwise `false`.
        bool request_stop() noexcept
        {
            if (!handle_)
                return false;

            auto& promise = handle_.promise();
            if (promise.has_external_stop_token_)
                return false;
            if (!promise.stop_token_.stop_possible())
                promise.stop_token_ = promise.stop_source_.get_token();
            return promise.stop_source_.request_stop();
        }

        /// @brief Indicates whether this task has a stop token available to its body.
        [[nodiscard]] bool stop_possible() const noexcept { return handle_ && handle_.promise().stop_token_.stop_possible(); }

        /// @brief Indicates whether this task's observed stop token is already cancelled.
        [[nodiscard]] bool stop_requested() const noexcept { return handle_ && handle_.promise().stop_token_.stop_requested(); }

        /// @brief Returns the stop token observed by the coroutine body.
        /// @return An empty token for an empty task, otherwise the effective internal or external token.
        [[nodiscard]] std::stop_token stop_token() const noexcept { return handle_ ? handle_.promise().stop_token_ : std::stop_token {}; }

        /// @brief Gives this task a stop token before it starts.
        /// @param token The token to observe; inherited by every task this one awaits.
        /// @return This task, so it can be passed straight to spawn().
        /// @note Set it before the task runs. A task already suspended keeps whatever it inherited.
        task&& with_stop_token(std::stop_token token) && noexcept
        {
            if (handle_)
            {
                handle_.promise().stop_token_ = std::move(token);
                handle_.promise().has_external_stop_token_ = true;
            }

            return std::move(*this);
        }

        /// @brief Registers the awaiting coroutine as the continuation.
        /// @param continuation The coroutine that will resume after this task finishes.
        /// @return The coroutine handle to resume for symmetric transfer.
        template <typename P>
        coroutine_handle_t await_suspend(std::coroutine_handle<P> continuation) noexcept
        {
            // Inherit the awaiting coroutine's stop token, so cancelling the outermost task reaches every task
            // it is waiting on. An explicit token already set on this task wins, so a sub-task can be given a
            // narrower scope than its parent.
            if constexpr (requires { continuation.promise().stop_token_; })
                if (!handle_.promise().stop_token_.stop_possible())
                    handle_.promise().stop_token_ = continuation.promise().stop_token_;

            handle_.promise().continuation_ = continuation;
            return handle_;
        }

        /// @brief Resumes the task and retrieves the result.
        /// @return The task result for non-void tasks.
        /// @throws The exception stored in the promise if one occurred.
        /// @throws std::bad_variant_access if result is missing (unlikely).
        [[nodiscard]] T await_resume() const noexcept(false)
        {
            // Taken out of the promise rather than read from it: this is the point where responsibility
            // for the failure passes to the awaiting coroutine, and what the destructor finds still
            // there is by definition an exception nobody ever collected.
            if (std::exception_ptr exception = std::exchange(handle_.promise().exception_, {}))
                std::rethrow_exception(exception);

            if constexpr (!std::is_void_v<T>)
                return std::get<1u>(std::move(handle_.promise().result_));
        }

    private:
        /// @brief Reports the exception the coroutine ended with, if nothing ever collected it.
        /// @note Only a finished coroutine can hold one; a task abandoned before it ran, or moved from,
        ///       has nothing to report. The exception is rethrown into a local catch purely to reach
        ///       what() - it cannot escape, which is what the destructor's noexcept needs.
        void report_unretrieved_exception() const noexcept;

        /// @brief The owned coroutine handle, if any.
        handle_type handle_ {};
    };

    template <typename T>
    void task<T>::report_unretrieved_exception() const noexcept
    {
        if (!handle_.done() || !handle_.promise().exception_)
            return;

        try
        {
            std::rethrow_exception(handle_.promise().exception_);
        }
        catch (const std::exception& e)
        {
            logger::log(logger::level::error, std::source_location::current(),
                        "task destroyed without ever being awaited; its exception is lost: {}", e.what());
        }
        catch (...)
        {
            logger::log(logger::level::error, std::source_location::current(),
                        "task destroyed without ever being awaited; its exception is lost");
        }
    }

    template <typename T>
    task<T> promise<T>::get_return_object() noexcept(false)
    {
        return task<T>(std::coroutine_handle<promise<T>>::from_promise(*this));
    }

    inline task<void> promise<void>::get_return_object() noexcept(false)
    {
        return task<void>(std::coroutine_handle<promise<void>>::from_promise(*this));
    }

    /// @brief Task yielding success or an error for operations that produce no payload.
    using task_returning_expected_void_t = task<expected_void_t>;

    /// @brief Task yielding a boolean result or the error that stopped the operation.
    using task_returning_expected_bool_t = task<expected_bool_t>;

    /// @brief Task yielding an integer result or the error that stopped the operation.
    using task_returning_expected_int_t = task<expected_int_t>;

    /// @brief Task yielding a byte count or the error that stopped the operation.
    /// @details This is the result type used by asynchronous read/write/send/receive operations.
    using task_returning_expected_size_t = task<expected_size_t>;

}
