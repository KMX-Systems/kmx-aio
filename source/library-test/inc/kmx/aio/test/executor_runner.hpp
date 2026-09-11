/// @file inc/kmx/aio/test/executor_runner.hpp
/// @brief Runs a task to completion on an executor, and waits for a flag with a deadline.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/task.hpp>

    #include <atomic>
    #include <chrono>
    #include <optional>
    #include <thread>
    #include <utility>
#endif

namespace kmx::aio::test
{
    /// @brief Waits for @p flag to become true, up to @p limit.
    /// @return True if the flag was observed set, false on timeout.
    [[nodiscard]] inline bool wait_for_flag(const std::atomic_bool& flag, const std::chrono::milliseconds limit)
    {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (!flag.load(std::memory_order_acquire))
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return true;
    }

    namespace detail
    {
        /// @brief Awaits @p work, stores what it returned, and ends the loop.
        /// @details The result cannot be returned: a spawned task is detached, so nobody is waiting on
        ///          this coroutine to hand anything back. Writing through a slot the caller owns is how
        ///          the value crosses back out.
        /// @tparam Executor The executor type; either completion or readiness.
        /// @tparam T What @p work produces.
        /// @param exec The executor whose loop to stop once the work is done.
        /// @param work The task to await.
        /// @param slot Where to put the result; engaged exactly when the task completed.
        /// @return A task the caller spawns.
        template <typename Executor, typename T>
        task<void> capture_awaited(Executor& exec, task<T> work, std::optional<T>& slot) noexcept(false)
        {
            slot.emplace(co_await std::move(work));
            exec.stop();
        }

        /// @brief Awaits @p work and ends the loop, for work that produces nothing.
        /// @tparam Executor The executor type; either completion or readiness.
        /// @param exec The executor whose loop to stop once the work is done.
        /// @param work The task to await.
        /// @param done Set to true once the task completed.
        /// @return A task the caller spawns.
        template <typename Executor>
        task<void> capture_awaited_void(Executor& exec, task<void> work, bool& done) noexcept(false)
        {
            co_await std::move(work);
            done = true;
            exec.stop();
        }
    }

    /// @brief Runs @p work to completion on @p exec and gives back what it returned.
    /// @details This is the shape almost every test that drives a single operation needs: spawn
    ///          something that awaits it, have that something record the result and stop the loop, run
    ///          the loop, then look at what was recorded. Written out by hand it takes a shared state
    ///          object, a coroutine to fill it, and four more lines to drive and unpack - none of which
    ///          says anything about the operation being tested.
    /// @note The stop() is what keeps a regression from hanging the binary: an operation that never
    ///       completes leaves run() with nothing to do but wait, and a hung test reports nothing at all.
    ///       Here it is unconditional and in one place, rather than repeated at every early return.
    /// @tparam Executor The executor type; either completion or readiness.
    /// @tparam T What @p work produces.
    /// @param exec The executor to run; must not already be running.
    /// @param work The task to await.
    /// @return The task's result, or nothing if it never completed.
    template <typename Executor, typename T>
    [[nodiscard]] std::optional<T> run_awaited(Executor& exec, task<T> work) noexcept(false)
    {
        std::optional<T> result;
        // Named, so the coroutine frame outlives run() - see completion::executor::spawn.
        auto driver = detail::capture_awaited(exec, std::move(work), result);
        exec.spawn(std::move(driver));
        exec.run();
        return result;
    }

    /// @brief Runs @p work to completion on @p exec, for work that produces nothing.
    /// @tparam Executor The executor type; either completion or readiness.
    /// @param exec The executor to run; must not already be running.
    /// @param work The task to await.
    /// @return True when the task ran to completion.
    template <typename Executor>
    [[nodiscard]] bool run_awaited_void(Executor& exec, task<void> work) noexcept(false)
    {
        bool done {};
        auto driver = detail::capture_awaited_void(exec, std::move(work), done);
        exec.spawn(std::move(driver));
        exec.run();
        return done;
    }

}
