/// @file aio/test/executor_runner.hpp
/// @brief Runs a readiness executor on its own thread, with a deadline.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// readiness::executor::run() returns when its work drains or when stop() is called, and a test for
/// shutdown behaviour is precisely a test of whether that happens. Calling run() on the test thread
/// therefore turns every such regression into a hung test binary, which reports nothing and takes the
/// rest of the suite with it.
///
/// This runs the loop on a separate thread and waits for it with a deadline, so a regression fails the
/// test instead. The executor is stopped on the way out whatever happened, so the thread is always
/// joinable and the failure is reported rather than hung.
#pragma once
#ifndef PCH
    #include <atomic>
    #include <chrono>
    #include <optional>
    #include <thread>
    #include <utility>

    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/task.hpp>
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

    /// @brief Runs an executor's event loop on a separate thread for the lifetime of this object.
    class scoped_runner
    {
    public:
        explicit scoped_runner(readiness::executor& exec) noexcept(false):
            exec_(exec),
            thread_(
                [this]()
                {
                    exec_.run();
                    finished_.store(true, std::memory_order_release);
                })
        {
        }

        scoped_runner(const scoped_runner&) = delete;
        scoped_runner& operator=(const scoped_runner&) = delete;

        /// @brief Stops the loop so the thread can be joined even when the test failed.
        /// @details Asked repeatedly on purpose. executor::stop() does nothing unless the loop is
        ///          already running, so a stop that lands before run() has begun is silently lost - and
        ///          the join that follows would then wait for a loop nobody will ever end. Whether that
        ///          race happens depends only on which of the two threads gets there first, which is why
        ///          it shows up as an occasional hang rather than a reliable one.
        ~scoped_runner() noexcept
        {
            while (!finished_.load(std::memory_order_acquire))
            {
                exec_.stop();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        /// @brief Waits for run() to return of its own accord.
        /// @return True when the loop finished within @p limit, false on timeout - which is the
        ///         observable form of "a task is still outstanding and nothing will ever complete it".
        [[nodiscard]] bool wait_until_drained(const std::chrono::milliseconds limit) { return wait_for_flag(finished_, limit); }

    private:
        readiness::executor& exec_;
        std::atomic_bool finished_ {false};
        // Declared last so the flag it writes is constructed first, and joined first on destruction.
        std::jthread thread_;
    };

    /// @brief Runs a completion executor's event loop on a separate thread for the lifetime of this
    ///        object, and stops it reliably on the way out.
    /// @details The same shape as scoped_runner above, and for the same reason. completion::executor
    ///          arms itself inside run() - `running_.exchange(true)` and the I/O thread are both created
    ///          there - so a stop() issued by a test before run() has reached that point finds nothing
    ///          running and is silently discarded. run() then blocks with nobody left to end it, and the
    ///          join that follows waits forever. Whether that happens depends only on which of the two
    ///          threads gets there first, so it shows up as an occasional hung test rather than a
    ///          reliable one - and a hung test binary reports nothing and takes the rest of the suite
    ///          down with it.
    ///
    ///          Asking repeatedly is what closes the window: a stop that arrives too early is simply
    ///          followed by another.
    class scoped_completion_runner
    {
    public:
        explicit scoped_completion_runner(completion::executor& exec) noexcept(false):
            exec_(exec),
            thread_(
                [this]()
                {
                    exec_.run();
                    finished_.store(true, std::memory_order_release);
                })
        {
        }

        scoped_completion_runner(const scoped_completion_runner&) = delete;
        scoped_completion_runner& operator=(const scoped_completion_runner&) = delete;

        ~scoped_completion_runner() noexcept
        {
            while (!finished_.load(std::memory_order_acquire))
            {
                exec_.stop();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        /// @brief Waits for run() to return of its own accord.
        [[nodiscard]] bool wait_until_drained(const std::chrono::milliseconds limit) { return wait_for_flag(finished_, limit); }

    private:
        completion::executor& exec_;
        std::atomic_bool finished_ {false};
        // Declared last so the flag it writes is constructed first, and joined first on destruction.
        std::jthread thread_;
    };

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
    } // namespace detail

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
    bool run_awaited_void(Executor& exec, task<void> work) noexcept(false)
    {
        bool done = false;
        auto driver = detail::capture_awaited_void(exec, std::move(work), done);
        exec.spawn(std::move(driver));
        exec.run();
        return done;
    }

} // namespace kmx::aio::test
