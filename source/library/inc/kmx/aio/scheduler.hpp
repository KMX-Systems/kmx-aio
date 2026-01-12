#pragma once
#ifndef PCH
    #include <condition_variable>
    #include <deque>
    #include <functional>
    #include <mutex>
    #include <stop_token>
    #include <thread>
    #include <vector>
#endif

namespace kmx::aio
{
    /// @brief A thread-pool scheduler that executes submitted tasks.
    class scheduler
    {
    public:
        /// @brief Constructs the scheduler and starts worker threads.
        /// @throws std::system_error if thread creation fails.
        /// @throws std::bad_alloc if memory allocation fails.
        explicit scheduler(std::uint32_t thread_count = 1u) noexcept(false);

        ~scheduler() noexcept;

        /// @brief Schedules a task for execution.
        /// @throws std::bad_alloc if queue allocation fails.
        void spawn(std::move_only_function<void()>&& task) noexcept(false);

    private:
        void run_worker(std::stop_token st) noexcept;

        std::vector<std::jthread> workers_;
        mutable std::mutex queue_mutex_;
        std::deque<std::move_only_function<void()>> queue_;
        std::condition_variable cv_;
    };

} // namespace kmx::aio
