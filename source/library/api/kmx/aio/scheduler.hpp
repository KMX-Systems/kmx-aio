/// @file aio/scheduler.hpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
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
    /// @details The scheduler owns a fixed set of worker threads and a protected
    /// task queue. Submitted callables are executed asynchronously on the next
    /// available worker.
    class scheduler
    {
    public:
        /// @brief Constructs the scheduler and starts worker threads.
        /// @param thread_count The number of worker threads to launch.
        /// @throws std::system_error if thread creation fails.
        /// @throws std::bad_alloc if memory allocation fails.
        explicit scheduler(std::uint32_t thread_count = 1u) noexcept(false);

        /// @brief Stops the workers and releases scheduler resources.
        ~scheduler() noexcept;

        /// @brief Schedules a task for execution.
        /// @param task The callable to execute on a worker thread.
        /// @throws std::bad_alloc if queue allocation fails.
        void spawn(std::move_only_function<void()>&& task) noexcept(false);

        /// @brief Indicates whether the calling thread is one of this scheduler's workers.
        /// @details Lets an owner tell "someone asked me to shut down" from "one of my own threads asked
        ///          me to shut down", which are not the same request: a worker cannot join itself.
        [[nodiscard]] bool is_worker_thread() const noexcept;

        /// @brief Blocks until the queue is empty and no worker is running a task.
        /// @details The point is ownership, not tidiness. A queued task may hold the last reference to
        ///          whatever owns this scheduler, and dropping that reference on a worker would run the
        ///          owner's destructor on a thread the owner is about to join - a thread joining itself.
        ///          An owner waits here before letting its own reference go.
        /// @note    Returns immediately when called from a worker, which would otherwise wait for a task
        ///          that cannot finish until the wait does.
        void wait_until_idle() noexcept;

    private:
        /// @brief Worker thread main loop.
        /// @param st The cooperative stop token for the worker.
        void run_worker(std::stop_token st) noexcept;

        /// @brief The owned worker threads.
        std::vector<std::jthread> workers_;
        /// @brief Serializes access to the task queue.
        mutable std::mutex queue_mutex_;
        /// @brief Pending tasks awaiting execution.
        std::deque<std::move_only_function<void()>> queue_;
        /// @brief Notifies workers when new tasks arrive or shutdown begins.
        std::condition_variable cv_;
        /// @brief Number of tasks currently being executed by a worker.
        /// @details Counted separately from the queue: a task that has been dequeued but not yet
        ///          finished still holds whatever it captured, and that is precisely the window
        ///          wait_until_idle() has to cover.
        std::size_t active_ {};
        /// @brief Notifies wait_until_idle() when the queue drains and the last task finishes.
        std::condition_variable idle_cv_;
    };

} // namespace kmx::aio
