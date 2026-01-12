#pragma once
#ifndef PCH
    #include <coroutine>
    #include <exception>
    #include <type_traits>
    #include <utility>
    #include <variant>

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/logger.hpp>
#endif

namespace kmx::aio
{
    template <typename T>
    class [[nodiscard]] task;

    namespace detail
    {
        struct promise_base
        {
            std::coroutine_handle<> continuation_;
            std::exception_ptr exception_;

            struct final_awaiter
            {
                bool await_ready() const noexcept { return false; }

                // Symmetric transfer to continuation
                template <typename P>
                    requires std::is_base_of_v<promise_base, P>
                std::coroutine_handle<> await_suspend(std::coroutine_handle<P> h) const noexcept
                {
                    if (h.promise().continuation_)
                        return h.promise().continuation_;

                    return std::noop_coroutine();
                }

                void await_resume() const noexcept {}
            };
        };

        template <typename T>
        struct promise: promise_base
        {
            // Variant to hold result or empty state
            std::variant<std::monostate, T> result_;

            // Creating the task object may throw if allocation fails, though unlikely for the wrapper itself.
            [[nodiscard]] task<T> get_return_object() noexcept(false);

            std::suspend_always initial_suspend() const noexcept { return {}; }

            final_awaiter final_suspend() const noexcept { return {}; }

            void unhandled_exception() noexcept { exception_ = std::current_exception(); }

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
            task<void> get_return_object() noexcept(false);

            std::suspend_always initial_suspend() const noexcept { return {}; }
            final_awaiter final_suspend() const noexcept { return {}; }

            void unhandled_exception() noexcept { exception_ = std::current_exception(); }

            void return_void() const noexcept {}
        };
    } // namespace detail

    /// @brief A lazy coroutine task.
    template <typename T = void>
    class [[nodiscard]] task
    {
    public:
        using promise_type = detail::promise<T>;
        using handle_type = std::coroutine_handle<promise_type>;

        task() noexcept = default;

        explicit task(handle_type h) noexcept: handle_(h) {}

        ~task() noexcept
        {
            if (handle_)
                handle_.destroy();
        }

        // Move-only
        task(const task&) = delete;
        task& operator=(const task&) = delete;

        task(task&& other) noexcept: handle_(std::exchange(other.handle_, nullptr)) {}

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

        bool await_ready() const noexcept { return !handle_ || handle_.done(); }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept
        {
            handle_.promise().continuation_ = continuation;
            return handle_;
        }

        /// @brief Resumes the task and retrieves the result.
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
