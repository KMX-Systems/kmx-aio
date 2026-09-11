/// @file inc/kmx/aio/benchmark/detail/driver.hpp
/// @brief Detached driver coroutine type used to await a task from ordinary code.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <coroutine>
    #include <exception>
#endif

namespace kmx::aio::benchmark::detail
{
    /// @brief Detached driver coroutine used to await a task from ordinary code.
    struct driver
    {
        struct promise_type
        {
            driver get_return_object() noexcept { return driver {std::coroutine_handle<promise_type>::from_promise(*this)}; }
            [[nodiscard]] std::suspend_always initial_suspend() const noexcept { return {}; }

            struct final_awaiter
            {
                [[nodiscard]] bool await_ready() const noexcept { return false; }
                void await_suspend(std::coroutine_handle<promise_type> h) const noexcept { h.destroy(); }
                void await_resume() const noexcept {}
            };

            [[nodiscard]] final_awaiter final_suspend() const noexcept { return {}; }
            void unhandled_exception() noexcept { std::terminate(); }
            void return_void() const noexcept {}
        };

        std::coroutine_handle<promise_type> handle;
    };
}
