/// @file inc/kmx/aio/benchmark/feature/watchdog.hpp
/// @brief A deadline that stops a benchmark run which has not finished, so one hung case cannot stall the suite.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <atomic>
    #include <chrono>
    #include <thread>
    #include <utility>
#endif

namespace kmx::aio::benchmark::feature
{
    /// @brief Stops a run that has not finished within a deadline, so one hung case cannot stall the suite.
    /// @tparam StopFn A callable that asks the run to stop. It is called from the watchdog's thread.
    template <typename StopFn>
    class watchdog
    {
    public:
        /// @brief Starts watching.
        /// @param stop What to call when the deadline passes.
        /// @param limit How long to wait before calling it.
        /// @throws std::system_error if the thread cannot be started.
        watchdog(StopFn stop, const std::chrono::seconds limit) noexcept(false):
            thread_([this, stop = std::move(stop), limit]() noexcept { watch(stop, limit); })
        {
        }

        /// @brief Stops watching and joins the thread.
        ~watchdog() noexcept { done_.store(true, std::memory_order_release); }

        watchdog(const watchdog&) = delete;
        watchdog& operator=(const watchdog&) = delete;

        /// @brief Whether the deadline passed.
        /// @return True when the run had to be stopped.
        [[nodiscard]] bool expired() const noexcept { return expired_.load(std::memory_order_relaxed); }

    private:
        /// @brief Polls the deadline until the run finishes, calling @p stop if it passes first.
        /// @param stop What to call when the deadline passes.
        /// @param limit How long to wait before calling it.
        void watch(const StopFn& stop, const std::chrono::seconds limit) noexcept;

        /// @brief Set when the run finished on its own.
        std::atomic_bool done_ {};

        /// @brief Set when the deadline passed first.
        std::atomic_bool expired_ {};

        /// @brief The watching thread. Declared last, so it starts only once the flags exist.
        std::jthread thread_;
    };

    template <typename StopFn>
    void watchdog<StopFn>::watch(const StopFn& stop, const std::chrono::seconds limit) noexcept
    {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (!done_.load(std::memory_order_acquire))
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                expired_.store(true, std::memory_order_relaxed);
                stop();
                return;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    /// @brief Deduction guide, so a lambda can be handed straight to the constructor.
    template <typename StopFn>
    watchdog(StopFn, std::chrono::seconds) -> watchdog<StopFn>;
}
