/// @file src/kmx/aio/completion/timer_test.cpp
/// @brief Regression tests for completion::timer native io_uring timeout waits.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/completion/timer.hpp>
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/task.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <chrono>
    #include <memory>
#endif

namespace kmx::aio::test::completion::timer_test
{
    using namespace kmx::aio::completion;

    struct wait_state
    {
        bool completed {};
        bool ok {};
        std::error_code error {};
        std::chrono::steady_clock::time_point start {};
        std::chrono::steady_clock::time_point end {};
    };

    auto run_wait(executor& exec, std::shared_ptr<wait_state> state) -> task<void>
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

        exec.stop();
    }

    TEST_CASE("completion timer waits for the requested duration", "[completion][timer]")
    {
        executor exec;
        auto state = std::make_shared<wait_state>();

        exec.spawn(run_wait(exec, state));
        exec.run();

        REQUIRE(state->completed);
        REQUIRE(state->ok);

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(state->end - state->start);
        REQUIRE(elapsed.count() >= 10);
    }

}
