/// @file aio/readiness/executor.hpp
/// @brief Readiness-model executor using epoll for event notification.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <atomic>
    #include <deque>
    #include <expected>
    #include <memory>
    #include <mutex>
    #include <sys/epoll.h>
    #include <unordered_map>

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/executor_base.hpp>
    #include <kmx/aio/readiness/descriptor/epoll.hpp>
    #include <kmx/aio/scheduler.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::readiness
{
    struct executor_config
    {
        std::uint32_t thread_count = 1u;
        std::uint32_t max_events = 1024u;
        port_t timeout_ms = 200u;
    };

    /// @brief Statistics for epoll operations and executor performance.
    struct statistics
    {
        std::atomic_uint64_t total_registrations {};
        std::atomic_uint64_t total_unregistrations {};
        std::atomic_uint64_t total_epoll_waits {};
        std::atomic_uint64_t total_events_received {};
        std::atomic_uint64_t timeout_count {};
        std::atomic_uint64_t error_count {};
        std::atomic_uint64_t total_tasks_spawned {};
        std::atomic_uint64_t total_tasks_completed {};

        /// @brief Reset all statistics counters.
        void reset() noexcept;
    };

    /// @brief Readiness execution engine handling epoll I/O and task scheduling.
    class executor: public executor_base, public std::enable_shared_from_this<executor>
    {
    public:
        /// @brief Constructs the executor.
        /// @throws std::system_error If epoll creation fails.
        /// @throws std::bad_alloc If scheduler creation fails.
        explicit executor(const executor_config& config = {}) noexcept(false);

        ~executor() noexcept;

        /// @brief Registers a file descriptor for edge-triggered events.
        [[nodiscard]] std::expected<void, std::error_code> register_fd(fd_t fd) noexcept;

        /// @brief Unregisters a file descriptor.
        void unregister_fd(fd_t fd) noexcept;

        /// @brief Awaits a specific event on a file descriptor.
        [[nodiscard]] auto wait_io(fd_t fd, event_type type) noexcept
        {
            struct io_awaiter
            {
                executor& exec;
                fd_t fd;
                event_type type;

                bool await_ready() const noexcept { return false; }

                // Subscription might throw (e.g. allocation in map), so await_suspend is noexcept(false)
                void await_suspend(std::coroutine_handle<> h) noexcept(false) { exec.subscribe(fd, type, h); }

                void await_resume() const noexcept {}
            };

            return io_awaiter {*this, fd, type};
        }

        /// @brief Submits a root task to the system.
        /// @throws std::bad_alloc if scheduling fails.
        void spawn(task<void>&& t) noexcept(false);

        /// @brief Starts the event loop. Blocks until stop is requested.
        void run() noexcept(false);

        /// @brief Signals the executor to stop.
        void stop() noexcept;

        /// @brief Returns a reference to the executor's statistics.
        [[nodiscard]] const statistics& get_stats() const noexcept { return metrics_; }

        /// @brief Reset all executor statistics.
        void reset_stats() noexcept { metrics_.reset(); }

    private:
        struct event_key
        {
            fd_t fd;
            event_type type;

            [[nodiscard]] auto operator<=>(const event_key&) const = default;
        };

        struct event_key_hash
        {
            [[nodiscard]] std::size_t operator()(const event_key& k) const noexcept
            {
                return std::hash<int> {}(k.fd) ^ (std::hash<int> {}(static_cast<int>(k.type)) << 1);
            }
        };

        // Internal use: register a coroutine to be resumed on an event.
        void subscribe(fd_t fd, event_type type, std::coroutine_handle<> handle) noexcept(false);

        // Internal loop function.
        void process_events(std::stop_token st) noexcept(false);

        void resume_if_found(fd_t fd, event_type type);

        // Helper for executing tasks and updating statistics.
        struct detached_task_wrapper
        {
            struct promise_type
            {
                detached_task_wrapper get_return_object() noexcept
                {
                    return detached_task_wrapper {std::coroutine_handle<promise_type>::from_promise(*this)};
                }

                std::suspend_always initial_suspend() const noexcept { return {}; }

                struct final_awaiter
                {
                    bool await_ready() const noexcept { return false; }
                    void await_suspend(std::coroutine_handle<promise_type> h) const noexcept { h.destroy(); }
                    void await_resume() const noexcept {}
                };

                final_awaiter final_suspend() const noexcept { return {}; }
                void unhandled_exception() noexcept { std::terminate(); }
                void return_void() const noexcept {}
            };

            std::coroutine_handle<promise_type> handle;
        };

        detached_task_wrapper execute_task(task<void> t, std::shared_ptr<executor> self) noexcept;

        executor_config config_;
        std::shared_ptr<scheduler> scheduler_;
        descriptor::epoll epoll_fd_;

        std::unordered_map<event_key, std::deque<std::coroutine_handle<>>, event_key_hash> subscribers_;
        std::mutex subscribers_mutex_;

        mutable statistics metrics_;
    };

} // namespace kmx::aio::readiness
