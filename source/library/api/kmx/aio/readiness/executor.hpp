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
    #include <sys/socket.h>
    #include <unordered_map>
    #include <unordered_set>

    #include <kmx/aio/executor_base.hpp>
    #include <kmx/aio/readiness/basic_types.hpp>
    #include <kmx/aio/readiness/descriptor/epoll.hpp>
    #include <kmx/aio/scheduler.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::readiness
{
    /// @brief Selects which readiness backend the executor drives its event loop with.
    enum class backend_mode : std::uint8_t
    {
        epoll_only,           ///< Use epoll backend only.
        openonload_preferred, ///< Prefer OpenOnload when available, fallback to epoll.
        openonload_required   ///< Require OpenOnload; fail construction if unavailable.
    };

    /// @brief The backend an executor actually settled on after applying its @ref backend_mode.
    enum class active_backend : std::uint8_t
    {
        /// @brief The epoll backend.
        epoll,
        /// @brief The OpenOnload accelerated backend.
        openonload
    };

    /// @brief Where a coroutine continues after the descriptor it waited on becomes ready.
    enum class resumption_mode : std::uint8_t
    {
        /// @brief Hand the resumption to a scheduler worker.
        /// @details Keeps the event loop free to reap the next event while application code runs on
        ///          another thread, at the cost of a wake-up and a context switch on every I/O
        ///          completion - and of application code that may run on any worker.
        scheduler,

        /// @brief Continue on the I/O thread that observed the event.
        /// @details The thread-per-core arrangement: no hand-off, no cross-core cache traffic, and
        ///          every coroutine of this executor runs on the core the executor is pinned to, so
        ///          state reached only from those coroutines needs no synchronization. The event loop
        ///          is blocked for as long as the coroutine runs, so a resumption that blocks - a
        ///          synchronous read, a lock held by another thread, heavy computation - delays every
        ///          other descriptor this executor serves.
        /// @note Cancellations still go through the scheduler when they arrive from another thread:
        ///       cancel_io() and unregister_fd() may be called anywhere, and resuming a coroutine on a
        ///       thread that merely happened to cancel it is not what this mode promises.
        inline_on_io_thread
    };

    /// @brief Construction parameters for @ref executor.
    struct executor_config
    {
        /// @brief Number of scheduler worker threads to create.
        std::uint32_t thread_count = 1u;
        /// @brief Maximum number of events reaped per `epoll_wait` call.
        std::uint32_t max_events = 1024u;
        /// @brief `epoll_wait` timeout, in milliseconds; also bounds how long a stop takes to be noticed.
        port_t timeout_ms = 200u;
        std::int16_t core_id = -1; ///< CPU core affinity (-1 = no pinning). Range: -1 to 16000.
        /// @brief Which backend to use, and whether OpenOnload is optional or mandatory.
        backend_mode backend = backend_mode::epoll_only;
        resumption_mode resumption = resumption_mode::scheduler; ///< Where ready coroutines continue.
    };

    /// @brief Statistics for epoll operations and executor performance.
    struct statistics
    {
        /// @brief Number of descriptors registered since the last reset.
        std::atomic_uint64_t total_registrations {};
        /// @brief Number of descriptors unregistered since the last reset.
        std::atomic_uint64_t total_unregistrations {};
        /// @brief Number of `epoll_wait` calls issued since the last reset.
        std::atomic_uint64_t total_epoll_waits {};
        /// @brief Number of events reaped from `epoll_wait` since the last reset.
        std::atomic_uint64_t total_events_received {};
        /// @brief Number of `epoll_wait` calls that returned without an event.
        std::atomic_uint64_t timeout_count {};
        /// @brief Number of failed epoll operations since the last reset.
        std::atomic_uint64_t error_count {};
        /// @brief Number of root tasks handed to @ref executor::spawn since the last reset.
        std::atomic_uint64_t total_tasks_spawned {};
        /// @brief Number of spawned root tasks that have run to completion.
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

        /// @brief Stops the event loop, joins the I/O thread, and releases the epoll descriptor.
        ~executor() noexcept;

        /// @brief Registers a file descriptor for edge-triggered events.
        [[nodiscard]] expected_void_t register_fd(const fd_t fd) noexcept;

        /// @brief Unregisters a file descriptor.
        void unregister_fd(const fd_t fd) noexcept;

        /// @brief Awaits a specific event on a file descriptor.
        /// @return True when the event fired, false when the wait was cancelled - by cancel_io() or by
        ///         unregister_fd() taking the descriptor away. A caller that ignores a false result
        ///         retries the operation that just reported EAGAIN and suspends again, which is how a
        ///         cancelled wait turns into a coroutine that never finishes.
        [[nodiscard]] auto wait_io(const fd_t fd, const event_type type) noexcept
        {
            struct io_awaiter
            {
                executor& exec;
                fd_t fd;
                event_type type;
                // Lives in the awaiting coroutine's frame, so its address stays valid for as long as the
                // subscription that points at it. Written only by the thread that cancels the wait,
                // before the handle is resumed, and read only after that resumption.
                bool cancelled = false;

                bool await_ready() const noexcept { return false; }

                // Subscription might throw (e.g. allocation in map), so await_suspend is noexcept(false).
                // Returning false resumes the coroutine without suspending, which is what happens when
                // the descriptor was already cancelled: deciding that inside subscribe(), under the lock
                // cancellation itself takes, is what stops a cancel that lands between the caller's own
                // check and this subscription from being lost.
                bool await_suspend(coroutine_handle_t h) noexcept(false) { return exec.subscribe(fd, type, h, &cancelled); }

                [[nodiscard]] bool await_resume() const noexcept { return !cancelled; }
            };

            return io_awaiter {*this, fd, type};
        }

        /// @brief Cancels every wait_io() currently suspended on @p fd.
        /// @details Each waiting coroutine is resumed with a cancelled result, so it can unwind and let
        ///          the task holding it complete. Use this to interrupt an operation that is parked on a
        ///          descriptor which will never see another event - an accept() on a listener that is
        ///          being shut down, for instance.
        void cancel_io(const fd_t fd) noexcept;

        /// @brief Asynchronously receives a message from a socket using readiness notifications.
        /// @param fd Socket file descriptor.
        /// @param msg Message descriptor for buffers/ancillary data.
        /// @param flags Flags forwarded to recvmsg.
        /// @return Number of bytes received or an error.
        [[nodiscard]] task_returning_expected_size_t async_recvmsg(const fd_t fd, ::msghdr* msg, const unsigned flags = 0u) noexcept(false);

        /// @brief Asynchronously sends a message on a socket using readiness notifications.
        /// @param fd Socket file descriptor.
        /// @param msg Message descriptor for buffers/ancillary data.
        /// @param flags Flags forwarded to sendmsg.
        /// @return Number of bytes sent or an error.
        [[nodiscard]] task_returning_expected_size_t async_sendmsg(const fd_t fd, const ::msghdr* msg,
                                                                   const unsigned flags = 0u) noexcept(false);

        /// @brief Asynchronously waits for a relative timeout duration.
        /// @param duration_ns Timeout duration in nanoseconds.
        /// @return Success or an error.
        [[nodiscard]] task_returning_expected_void_t async_timeout(const std::uint64_t duration_ns) noexcept(false);

        /// @brief Submits a root task to the system.
        /// @throws std::bad_alloc if scheduling fails.
        /// @warning The executor is not owned by the spawned task. The caller must keep this
        ///          executor object alive until the task completes.
        /// @warning A lambda coroutine does not own its closure: the closure object is a temporary
        ///          destroyed at the end of the full-expression, while the coroutine frame keeps a
        ///          pointer into it. Spawning one directly - spawn([&]() -> task<void> { ... }()) -
        ///          therefore leaves every capture dangling from the first suspension onwards. Give
        ///          the lambda a name that outlives the run, or spawn a coroutine function instead,
        ///          whose parameters are copied into the frame:
        ///          @code
        ///          auto body = [&]() -> task<void> { ... };   // outlives exec.run()
        ///          exec.spawn(body());
        ///          @endcode
        void spawn(task<void>&& t) noexcept(false);

        /// @brief Starts the event loop. Blocks until stop is requested.
        void run() noexcept(false);

        /// @brief Signals the executor to stop.
        void stop() noexcept;

        /// @brief Returns a reference to the executor's statistics.
        [[nodiscard]] const statistics& get_stats() const noexcept { return metrics_; }

        /// @brief Returns the backend currently selected by the executor.
        [[nodiscard]] active_backend get_active_backend() const noexcept { return active_backend_; }

        /// @brief Checks whether the I/O thread is affined to the requested CPU core.
        /// @details Returns an error if the I/O thread is not currently running.
        [[nodiscard]] expected_bool_t is_io_thread_affined_to(int core_id) noexcept;

        /// @brief Reset all executor statistics.
        void reset_stats() noexcept { metrics_.reset(); }

    private:
        /// @brief Key identifying one subscription slot: a descriptor paired with the event awaited on it.
        struct event_key
        {
            /// @brief The descriptor being awaited.
            fd_t fd;
            /// @brief The event the waiters are suspended on.
            event_type type;

            /// @brief Orders and compares keys field by field.
            /// @return The three-way comparison result over (fd, type).
            [[nodiscard]] auto operator<=>(const event_key&) const = default;
        };

        /// @brief Hash functor making @ref event_key usable as an unordered-map key.
        struct event_key_hash
        {
            /// @brief Combines the descriptor and event-type hashes.
            /// @param k The key to hash.
            /// @return The hash value for @p k.
            [[nodiscard]] std::size_t operator()(const event_key& k) const noexcept
            {
                return std::hash<int> {}(k.fd) ^ (std::hash<int> {}(static_cast<int>(k.type)) << 1);
            }
        };

        // Internal use: register a coroutine to be resumed on an event. Returns false when the
        // descriptor is already cancelled, in which case nothing is stored and the caller must not
        // suspend; *cancelled is set so the awaiter reports the wait as cancelled.
        // True when the calling thread is one this executor owns - the I/O thread or a scheduler
        // worker - and therefore cannot join the I/O thread.
        /// @brief True when the calling thread is one this executor owns - the I/O thread or a scheduler
        ///        worker - and therefore cannot join the I/O thread.
        [[nodiscard]] bool on_owned_thread() const noexcept;

        /// @brief Registers a coroutine to be resumed when @p type fires on @p fd.
        /// @details Refuses the subscription when the descriptor is already cancelled, so a cancel landing
        ///          between the caller's own check and this call cannot be lost.
        /// @param fd        The descriptor to wait on.
        /// @param type      The event to wait for.
        /// @param handle    The coroutine to resume once the event fires.
        /// @param cancelled Flag in the awaiting coroutine's frame, set when the wait is cancelled.
        /// @return `false` when the descriptor is already cancelled and the caller must not suspend.
        /// @throws std::bad_alloc If the subscription map could not grow.
        [[nodiscard]] bool subscribe(const fd_t fd, const event_type type, coroutine_handle_t handle,
                                     bool* const cancelled) noexcept(false);

        // Resumes every waiter on fd, flagging each as cancelled first.
        // @param remember Keep the descriptor marked so a subscription arriving afterwards is refused
        //                 too. Wanted when the descriptor stays registered and someone may still try to
        //                 wait on it (cancel_io); not wanted when it is being taken away for good
        //                 (unregister_fd), where the mark would outlive everything that could consult it.
        /// @brief Resumes every waiter on @p fd, flagging each as cancelled first.
        /// @param fd       The descriptor whose waiters are being released.
        /// @param remember Keep the descriptor marked so a subscription arriving afterwards is refused
        ///                 too. Wanted when the descriptor stays registered and someone may still try to
        ///                 wait on it (cancel_io); not wanted when it is being taken away for good
        ///                 (unregister_fd), where the mark would outlive everything that could consult it.
        void cancel_waiters(const fd_t fd, const bool remember) noexcept;

        // Internal loop function.
        /// @brief Body of the event loop: reaps epoll events and resumes their waiters until stopped.
        /// @param st The stop token that ends the loop.
        /// @throws std::bad_alloc If resuming a waiter through the scheduler fails to allocate.
        void process_events(std::stop_token st) noexcept(false);

        /// @brief Pins the calling thread to the configured CPU core.
        void pin_to_core() const noexcept;

        /// @brief Resumes the first waiter subscribed to (@p fd, @p type), if any.
        /// @param fd   The descriptor the event fired on.
        /// @param type The event that fired.
        void resume_if_found(const fd_t fd, const event_type type);

        /// @brief Wakes the event loop out of epoll_wait immediately.
        /// @details Written to when the loop is asked to stop. Without it the loop learns of a stop
        ///          only when its epoll_wait times out, so a shutdown takes up to timeout_ms - a fifth
        ///          of a second by default - during which the caller of stop() or run() is simply
        ///          waiting for a timer to expire.
        void wake_event_loop() const noexcept;

        /// @brief Drains the wake-up descriptor after it has fired.
        void drain_wake_events() const noexcept;

        /// @brief True when the calling thread is this executor's I/O thread.
        [[nodiscard]] bool on_io_thread() const noexcept;

        /// @brief Resumes a coroutine, either here or on a scheduler worker, as configured.
        /// @param handle The coroutine to resume.
        void resume_waiter(coroutine_handle_t handle) noexcept;

        // Helper for executing tasks and updating statistics.
        /// @brief Detached coroutine wrapper that runs a spawned task and updates the task statistics.
        struct detached_task_wrapper
        {
            /// @brief Promise type of @ref detached_task_wrapper; destroys its own frame on completion.
            struct promise_type
            {
                /// @brief Builds the wrapper handed back to @ref execute_task.
                /// @return A wrapper owning the coroutine handle for this promise.
                detached_task_wrapper get_return_object() noexcept
                {
                    return detached_task_wrapper {std::coroutine_handle<promise_type>::from_promise(*this)};
                }

                /// @brief Suspends before the body runs, so the caller decides when to start it.
                /// @return An always-suspending awaiter.
                std::suspend_always initial_suspend() const noexcept { return {}; }

                /// @brief Final awaiter that destroys the coroutine frame instead of resuming anyone.
                struct final_awaiter
                {
                    /// @brief Never completes synchronously, so @ref await_suspend always runs.
                    /// @return Always `false`.
                    bool await_ready() const noexcept { return false; }
                    /// @brief Destroys the finished coroutine frame.
                    /// @param h The handle of the coroutine that just completed.
                    void await_suspend(std::coroutine_handle<promise_type> h) const noexcept { h.destroy(); }
                    // LCOV_EXCL_LINE: await_suspend above destroys the frame, so nothing resumes to
                    // run this. It exists because the awaiter concept asks for it.
                    /// @brief Required by the awaiter concept; never reached because the frame is destroyed above.
                    void await_resume() const noexcept {} // LCOV_EXCL_LINE
                };

                /// @brief Returns the awaiter that tears the frame down.
                /// @return The @ref final_awaiter.
                final_awaiter final_suspend() const noexcept { return {}; }
                // LCOV_EXCL_LINE: reaching this ends the process, so no test can take it and return.
                // execute_task() catches std::exception around the whole body, which leaves only a
                // throw of something not derived from it - and there is no sane way to continue from
                // that inside a detached task whose frame is about to be destroyed.
                /// @brief Terminates: a detached task whose frame is about to be destroyed cannot propagate.
                void unhandled_exception() noexcept { std::terminate(); } // LCOV_EXCL_LINE
                /// @brief Completes the coroutine; the task itself returns nothing.
                void return_void() const noexcept {}
            };

            /// @brief Handle of the wrapped coroutine.
            std::coroutine_handle<promise_type> handle;
        };

        /// @brief Runs a spawned task to completion and updates the task counters.
        /// @param t    The task to run.
        /// @param self Shared ownership of this executor, keeping it alive for the task's lifetime.
        /// @return The detached wrapper coroutine driving @p t.
        detached_task_wrapper execute_task(task<void> t, std::shared_ptr<executor> self) noexcept;

        /// @brief The configuration this executor was constructed with.
        executor_config config_;
        /// @brief The backend selected during construction.
        active_backend active_backend_ = active_backend::epoll;
        /// @brief The worker pool that resumes coroutines in @ref resumption_mode::scheduler.
        std::shared_ptr<scheduler> scheduler_;
        /// @brief The epoll instance every registered descriptor is attached to.
        descriptor::epoll epoll_fd_;

        /// @brief Descriptor the loop watches so it can be woken on demand.
        /// @note Invalid when the eventfd could not be created, in which case the loop falls back to
        ///       noticing a stop when its wait times out, exactly as it did before.
        file_descriptor wake_fd_;

        /// @brief A coroutine suspended in wait_io(), and the flag telling it why it was resumed.
        /// @brief A coroutine suspended in wait_io(), and the flag telling it why it was resumed.
        struct waiter
        {
            /// @brief The suspended coroutine to resume.
            coroutine_handle_t handle;
            /// @brief Points into the awaiter's frame; set before resuming a cancelled wait.
            bool* cancelled;
        };

        /// @brief Waiters parked on each (descriptor, event) pair, in arrival order.
        std::unordered_map<event_key, std::deque<waiter>, event_key_hash> subscribers_;

        /// @brief Descriptors whose waits are cancelled, so a subscription arriving after the
        ///        cancellation is refused instead of parking forever. Cleared by register_fd(), which is
        ///        what re-arms a descriptor - including a recycled number belonging to a new socket.
        std::unordered_set<fd_t> cancelled_fds_;
        /// @brief Guards @ref subscribers_ and @ref cancelled_fds_.
        std::mutex subscribers_mutex_;
        // Guards all access to io_thread_ inherited from executor_base.
        /// @brief Guards all access to io_thread_ inherited from executor_base.
        mutable std::mutex io_thread_mutex_;

        /// @brief Counters reported by @ref get_stats.
        mutable statistics metrics_;
    };

} // namespace kmx::aio::readiness
