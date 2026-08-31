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
    #include <thread>

    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/readiness/executor.hpp>
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
            thread_([this]() { exec_.run(); finished_.store(true, std::memory_order_release); })
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
            thread_([this]() { exec_.run(); finished_.store(true, std::memory_order_release); })
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

} // namespace kmx::aio::test
