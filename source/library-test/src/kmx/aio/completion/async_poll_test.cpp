/// @file completion/async_poll_test.cpp
/// @brief Unit tests for completion::executor::async_poll() using pipe fds.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <array>
#include <memory>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::completion
{
    // ---------------------------------------------------------------------------
    // Test 1 — fd already readable before poll is submitted
    // ---------------------------------------------------------------------------

    struct poll_ready_state
    {
        bool completed {};
        bool ok {};
        int revents {};
        std::error_code error {};
    };

    auto run_poll_ready(executor& exec, std::shared_ptr<poll_ready_state> state, const int read_fd) -> task<void>
    {
        const auto result = co_await exec.async_poll(read_fd, POLLIN);
        state->completed = true;
        if (result)
        {
            state->ok = true;
            state->revents = *result;
        }
        else
            state->error = result.error();

        exec.stop();
    }

    TEST_CASE("async_poll resumes when fd is already readable", "[completion][executor][async_poll]")
    {
        // Write to the pipe BEFORE spawning the poll coroutine so the fd is immediately
        // readable. IORING_OP_POLL_ADD satisfies a level-triggered fd at submission time.
        std::array<int, 2u> fds {};
        REQUIRE(::pipe2(fds.data(), O_NONBLOCK | O_CLOEXEC) == 0);
        const int read_fd = fds[0u];
        const int write_fd = fds[1u];

        const std::byte sentinel {0x42u};
        REQUIRE(::write(write_fd, &sentinel, 1u) == 1);

        executor exec;
        auto state = std::make_shared<poll_ready_state>();

        exec.spawn(run_poll_ready(exec, state, read_fd));
        exec.run();

        ::close(write_fd);
        ::close(read_fd);

        REQUIRE(state->completed);
        REQUIRE(state->ok);
        REQUIRE((state->revents & POLLIN) != 0);
    }

    // ---------------------------------------------------------------------------
    // Test 2 — negative case: async_poll propagates errors for bad fds
    // ---------------------------------------------------------------------------

    struct poll_error_state
    {
        bool completed {};
        bool is_error {};
        std::error_code error {};
    };

    auto run_poll_bad_fd(executor& exec, std::shared_ptr<poll_error_state> state) -> task<void>
    {
        // fd -1 is always invalid; the kernel rejects it and io_uring returns EBADF.
        const auto result = co_await exec.async_poll(-1, POLLIN);
        state->completed = true;
        if (!result)
        {
            state->is_error = true;
            state->error = result.error();
        }

        exec.stop();
    }

    TEST_CASE("async_poll returns error for invalid fd", "[completion][executor][async_poll]")
    {
        executor exec;
        auto state = std::make_shared<poll_error_state>();

        exec.spawn(run_poll_bad_fd(exec, state));
        exec.run();

        REQUIRE(state->completed);
        REQUIRE(state->is_error);
        REQUIRE(state->error.value() != 0);
    }

} // namespace kmx::aio::completion
