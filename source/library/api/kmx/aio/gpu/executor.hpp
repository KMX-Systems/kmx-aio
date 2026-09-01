/// @file aio/gpu/executor.hpp
/// @brief GPU completion-model executor using CUDA streams and events.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <atomic>
    #include <coroutine>
    #include <cstdint>
    #include <deque>
    #include <memory>
    #include <mutex>
    #include <stop_token>
    #include <unordered_map>

    #include <kmx/aio/executor_base.hpp>
    #include <kmx/aio/gpu/basic_types.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::gpu
{
    /// @brief Configuration for the GPU completion-model executor.
    struct executor_config
    {
        std::uint32_t max_events = 256u; ///< Maximum GPU events to poll per cycle.
        std::uint32_t thread_count = 1u; ///< Number of worker threads for coroutine resumption.
        std::int16_t core_id = -1;       ///< CPU core affinity (-1 = no pinning). Range: -1 to 16000.
        std::int16_t gpu_device = 0;     ///< GPU device index. Range: 0 to 128. Use int16_t for alignment.
    };

    /// @brief Statistics for GPU operations and executor performance.
    struct statistics
    {
        std::atomic_uint64_t total_events_created {};   ///< Total GPU events created.
        std::atomic_uint64_t total_events_completed {}; ///< Total GPU events signaled.
        std::atomic_uint64_t total_tasks_spawned {};    ///< Total top-level tasks spawned.
        std::atomic_uint64_t total_tasks_completed {};  ///< Total top-level tasks completed.
        std::atomic_uint64_t error_count {};            ///< Total GPU errors encountered.
        std::atomic_uint64_t poll_timeout_count {};     ///< Times event polling timed out.

        /// @brief Default constructor (move-only, deletedcopy).
        statistics() noexcept = default;

        /// @brief Non-copyable (contains atomics).
        statistics(const statistics&) = delete;
        /// @brief Non-copyable (contains atomics).
        statistics& operator=(const statistics&) = delete;

        /// @brief Resets all counters to zero.
        void reset() noexcept;
    };

    /// @brief GPU completion-model executor using CUDA streams and events.
    /// @details Implements a share-nothing, thread-per-core reactor for GPU workloads.
    ///          GPU operations are submitted to streams; completion is signaled via events.
    ///          Coroutines are resumed when their specific GPU event fires.
    ///
    ///          ARCHITECTURE:
    ///            - Thread-per-core: One executor per GPU core (share-nothing model).
    ///            - GPU Device Binding: Fixed to a single GPU device per executor.
    ///            - Event-Driven Resumption: Coroutines awaiting GPU events are resumed
    ///              when events complete (via cudaEventQuery or cudaStreamWaitEvent).
    ///            - Deterministic Allocation: Coroutine frames allocated from thread-local
    ///              slab allocator (see kmx::aio::allocator).
    ///
    ///          SCOPE:
    ///            - Provides coroutine resumption on CUDA stream/event completion.
    ///            - Excludes inference orchestration and model lifecycle policy.
    ///            - Excludes multi-device scheduling logic.
    ///
    class executor: public executor_base, public std::enable_shared_from_this<executor>
    {
    public:
        /// @brief Constructs the executor and initializes CUDA for the target device.
        /// @param config Executor configuration.
        /// @throws std::system_error if CUDA initialization fails.
        /// @throws std::bad_alloc if internal allocations fail.
        explicit executor(const executor_config& config = {}) noexcept(false);

        /// @brief Stops the event loop, drains pending work, and releases the CUDA resources.
        ~executor() noexcept;

        /// @brief Non-copyable.
        executor(const executor&) = delete;
        /// @brief Non-copyable.
        executor& operator=(const executor&) = delete;

        /// @brief Spawns a task to be executed by this executor.
        /// @param coro The coroutine to spawn.
        /// @details The task is appended to the executor's work queue and will be
        ///          resumed by the I/O thread when GPU events it awaits fire.
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
        template <typename T>
        void spawn(task<T> coro) noexcept(false);

        /// @brief Runs the executor's main event loop.
        /// @details This function blocks until stop() is called or a fatal error occurs.
        ///          It:
        ///            - Polls CUDA events for completion.
        ///            - Resumes coroutines whose events have fired.
        ///            - Yields to the OS when idle.
        ///          This should be called from a dedicated thread (e.g., via std::jthread).
        void run(std::stop_token stop_token = {}) noexcept(false);

        /// @brief Signals the executor to stop and waits for graceful shutdown.
        /// @details Enqueues a stop signal and blocks until the executor's event loop exits.
        ///          Safe to call from any thread. Safe to call from a GPU coroutine
        ///          (detects self-stop and returns early).
        void stop() noexcept;

        /// @brief Retrieves the current statistics snapshot (const reference).
        [[nodiscard]] const statistics& get_statistics() const noexcept;

        /// @brief Resets all statistics counters.
        void reset_statistics() noexcept;

        /// @brief Registers a coroutine waiting on a GPU event.
        /// @details Intended for internal use by GPU event awaiters.
        void register_waiting_coroutine(event_handle event, coroutine_handle_t h) noexcept;

        /// @brief Gets the GPU device ID this executor is bound to.
        [[nodiscard]] int gpu_device() const noexcept { return config_.gpu_device; }

        /// @brief Gets the CPU core ID this executor is pinned to (-1 if no pinning).
        [[nodiscard]] int cpu_core() const noexcept { return config_.core_id; }

    private:
        /// @brief Detached wrapper for top-level spawned tasks.
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
                    /// @brief Required by the awaiter concept; never reached because the frame is destroyed above.
                    void await_resume() const noexcept {}
                };

                /// @brief Returns the awaiter that tears the frame down.
                /// @return The @ref final_awaiter.
                final_awaiter final_suspend() const noexcept { return {}; }
                /// @brief Terminates: a detached task whose frame is about to be destroyed cannot propagate.
                void unhandled_exception() noexcept { std::terminate(); }
                /// @brief Completes the coroutine; the task itself returns nothing.
                void return_void() const noexcept {}
            };

            /// @brief Handle of the wrapped coroutine.
            std::coroutine_handle<promise_type> handle;
        };

        /// @brief Runs a spawned task to completion and updates the task counters.
        /// @tparam T   The task's result type.
        /// @param t    The task to run.
        /// @param self Shared ownership of this executor, keeping it alive for the task's lifetime.
        /// @return The detached wrapper coroutine driving @p t.
        template <typename T>
        detached_task_wrapper execute_task(task<T> t, std::shared_ptr<executor> self) noexcept;

        /// @brief Tells whether any task or GPU event is still outstanding.
        /// @return `true` while the pending-task queue or the waiting-event map is non-empty.
        [[nodiscard]] bool has_pending_work() noexcept;

        /// @brief The configuration this executor was constructed with.
        executor_config config_;
        /// @brief Counters reported by @ref get_statistics.
        statistics stats_;
        /// @brief Spawned coroutines waiting for their first resumption.
        std::deque<coroutine_handle_t> pending_tasks_;
        /// @brief Maps each pending GPU event to the coroutine suspended on it.
        std::unordered_map<void*, coroutine_handle_t> waiting_events_;
        /// @brief Set by @ref stop to make the event loop exit.
        std::atomic_bool stop_requested_ {false};
        /// @brief Guards @ref pending_tasks_ and @ref waiting_events_.
        std::mutex queue_mutex_;

        /// @brief Pinned to a single CUDA GPU device.
        void set_gpu_device() noexcept(false);

        /// @brief Polls GPU events and resumes waiting coroutines.
        /// @return true if work was done, false if idle.
        /// @warning Never call this holding @ref queue_mutex_, and never resume a coroutine while it is
        ///          held: a resumption runs application code that may spawn a task or await another
        ///          event, and both of those take that same non-recursive mutex.
        [[nodiscard]] bool poll_events() noexcept;

        /// @brief Resumes one coroutine with this executor marked as the current one for the thread.
        /// @param handle The coroutine to resume.
        /// @details The marker is what @ref event::awaiter::await_suspend consults to decide between
        ///          registering with this executor and busy-waiting on the event itself, so it has to be
        ///          in place for the whole resumption. The previous value is restored rather than
        ///          cleared, so a resumption that drives a nested poll does not leave the outer one
        ///          running unmarked.
        /// @warning Must be called with @ref queue_mutex_ released.
        void resume_on_executor(coroutine_handle_t handle) noexcept;

        /// @brief Processes all pending GPU events (non-blocking).
        void process_events() noexcept;

        /// @brief Handles graceful shutdown after stop() is called.
        void finalize() noexcept;
    };

} // namespace kmx::aio::gpu
