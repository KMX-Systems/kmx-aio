/// @file inc/kmx/aio/test/scoped_completion_runner.hpp
/// @brief Runs a completion executor on its own thread, and stops it reliably on the way out.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/test/executor_runner.hpp>

    #include <atomic>
    #include <chrono>
    #include <thread>
#endif

namespace kmx::aio::test
{
    /// @brief Runs a completion executor's event loop on a separate thread for the lifetime of this
    ///        object, and stops it reliably on the way out.
    /// @details The same shape as scoped_runner, and for the same reason. completion::executor
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
}
