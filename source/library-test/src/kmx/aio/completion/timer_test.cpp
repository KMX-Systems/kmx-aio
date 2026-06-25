/// @file completion/timer_test.cpp
/// @brief Regression tests for completion::timer native io_uring timeout waits.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <chrono>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/timer.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::completion
{
    struct timer_state
    {
        bool completed {};
        bool ok {};
        std::error_code error {};
        std::chrono::steady_clock::time_point start {};
        std::chrono::steady_clock::time_point end {};
    };

    auto run_timer_wait(std::shared_ptr<executor> exec, std::shared_ptr<timer_state> state) -> task<void>
    {
        timer tmr {exec};
        state->start = std::chrono::steady_clock::now();

        const auto result = co_await tmr.wait(std::chrono::milliseconds(20));
        state->end = std::chrono::steady_clock::now();
        state->completed = true;
        if (result)
            state->ok = true;
        else
            state->error = result.error();

        exec->stop();
    }

    TEST_CASE("completion timer waits for the requested duration", "[completion][timer]")
    {
        auto exec = std::make_shared<executor>();
        auto state = std::make_shared<timer_state>();

        exec->spawn(run_timer_wait(exec, state));
        exec->run();

        REQUIRE(state->completed);
        REQUIRE(state->ok);

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(state->end - state->start);
        REQUIRE(elapsed.count() >= 10);
    }

} // namespace kmx::aio::completion
