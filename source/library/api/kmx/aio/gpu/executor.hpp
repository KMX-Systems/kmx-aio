/// @file aio/gpu/executor.hpp
/// @brief GPU completion-model executor using CUDA streams and events.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <atomic>
    #include <coroutine>
    #include <cstdint>
    #include <deque>
    #include <expected>
    #include <memory>
    #include <mutex>
    #include <span>
    #include <stop_token>
    #include <system_error>
    #include <unordered_map>

    #include <kmx/aio/executor_base.hpp>
    #include <kmx/aio/task.hpp>
#endif

/// @brief CUDA runtime is optional; guard GPU code with this feature flag.
#if defined(KMX_AIO_FEATURE_CUDA)
    #include <cuda_runtime.h>
#endif

namespace kmx::aio::gpu
{
    /// @brief Opaque handle to a GPU stream (CUDA stream or mock).
    using stream_handle = void*;

    /// @brief Opaque handle to a GPU event (CUDA event or mock).
    using event_handle = void*;

    /// @brief Forward declarations.
    class stream;
    class event;

    /// @brief Configuration for the GPU completion-model executor.
    struct executor_config
    {
        std::uint32_t max_events = 256u;   ///< Maximum GPU events to poll per cycle.
        std::uint32_t thread_count = 1u;   ///< Number of worker threads for coroutine resumption.
        std::int16_t core_id = -1;         ///< CPU core affinity (-1 = no pinning). Range: -1 to 16000.
        std::int16_t gpu_device = 0;       ///< GPU device index. Range: 0 to 128. Use int16_t for alignment.
    };

    /// @brief Statistics for GPU operations and executor performance.
    struct statistics
    {
        std::atomic_uint64_t total_events_created {};      ///< Total GPU events created.
        std::atomic_uint64_t total_events_completed {};    ///< Total GPU events signaled.
        std::atomic_uint64_t total_tasks_spawned {};       ///< Total top-level tasks spawned.
        std::atomic_uint64_t total_tasks_completed {};     ///< Total top-level tasks completed.
        std::atomic_uint64_t error_count {};               ///< Total GPU errors encountered.
        std::atomic_uint64_t poll_timeout_count {};        ///< Times event polling timed out.

        /// @brief Default constructor (move-only, deletedcopy).
        statistics() noexcept = default;

        /// @brief Non-copyable (contains atomics).
        statistics(const statistics&) = delete;
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
    ///          SCOPE (per Plan.md):
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

        ~executor() noexcept;

        /// @brief Non-copyable.
        executor(const executor&) = delete;
        /// @brief Non-copyable.
        executor& operator=(const executor&) = delete;

        /// @brief Spawns a task to be executed by this executor.
        /// @param coro The coroutine to spawn.
        /// @details The task is appended to the executor's work queue and will be
        ///          resumed by the I/O thread when GPU events it awaits fire.
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

        /// @brief Gets the GPU device ID this executor is bound to.
        [[nodiscard]] int gpu_device() const noexcept { return config_.gpu_device; }

        /// @brief Gets the CPU core ID this executor is pinned to (-1 if no pinning).
        [[nodiscard]] int cpu_core() const noexcept { return config_.core_id; }

    private:
        executor_config config_;
        statistics stats_;
        std::deque<std::coroutine_handle<>> pending_tasks_;
        std::unordered_map<void*, std::coroutine_handle<>> waiting_events_;
        std::atomic_bool stop_requested_ {false};
        std::mutex queue_mutex_;

        /// @brief Pinned to a single CUDA GPU device.
        void set_gpu_device() noexcept(false);

        /// @brief Polls GPU events and resumes waiting coroutines.
        /// @return true if work was done, false if idle.
        [[nodiscard]] bool poll_events() noexcept;

        /// @brief Processes all pending GPU events (non-blocking).
        void process_events() noexcept;

        /// @brief Handles graceful shutdown after stop() is called.
        void finalize() noexcept;
    };

    /// @brief GPU event awaiter for coroutine suspension on GPU completion.
    /// @details Provides a C++20 co_await interface for waiting on GPU events.
    ///          When a coroutine co_awaits an event, it suspends until the GPU work
    ///          that recorded the event has completed.
    class event
    {
    public:
        /// @brief Internal awaiter struct for co_await suspension.
        struct awaiter
        {
            event& event_;

            bool await_ready() const noexcept;
            void await_suspend(std::coroutine_handle<> h) noexcept;
            void await_resume() const noexcept {}
        };

        /// @brief Creates a new GPU event for tracking GPU stream work.
        /// @throws std::system_error if cudaEventCreate fails.
        event() noexcept(false);

        /// @brief Destroys the GPU event.
        ~event() noexcept;

        /// @brief Non-copyable.
        event(const event&) = delete;
        /// @brief Non-copyable.
        event& operator=(const event&) = delete;

        /// @brief Move constructor.
        event(event&& other) noexcept;

        /// @brief Move assignment.
        event& operator=(event&& other) noexcept;

        /// @brief Returns the underlying CUDA event handle.
        [[nodiscard]] event_handle handle() const noexcept { return handle_; }

        /// @brief C++20 awaiter interface for co_await.
        /// @return An awaiter that suspends the coroutine until the event fires.
        [[nodiscard]] awaiter operator co_await() noexcept;

        /// @brief Checks if the event has fired (non-blocking).
        /// @return true if the event is signaled, false otherwise.
        /// @throws std::system_error if cudaEventQuery fails.
        [[nodiscard]] bool is_ready() const noexcept(false);

    private:
        event_handle handle_ {};

        void destroy() noexcept;

        friend struct awaiter;
        friend class stream;
    };

    /// @brief GPU stream wrapper for CUDA stream operations.
    /// @details Provides a lightweight RAII wrapper around CUDA stream creation,
    ///          synchronization, and destruction. Non-copyable, move-only.
    class stream
    {
    public:
        /// @brief Creates a new GPU stream on the current device.
        /// @throws std::system_error if cudaStreamCreate fails.
        stream() noexcept(false);

        /// @brief Destroys the GPU stream (synchronizes if needed).
        ~stream() noexcept;

        /// @brief Non-copyable.
        stream(const stream&) = delete;
        /// @brief Non-copyable.
        stream& operator=(const stream&) = delete;

        /// @brief Move constructor.
        stream(stream&& other) noexcept;

        /// @brief Move assignment.
        stream& operator=(stream&& other) noexcept;

        /// @brief Returns the underlying CUDA stream handle.
        [[nodiscard]] stream_handle handle() const noexcept { return handle_; }

        /// @brief Synchronizes this stream (blocks until all queued work completes).
        /// @throws std::system_error if cudaStreamSynchronize fails.
        void synchronize() noexcept(false);

        /// @brief Records an event on this stream (for async synchronization).
        /// @return A GPU event that fires when all prior work on this stream completes.
        /// @throws std::system_error if cudaEventCreate or cudaEventRecord fails.
        [[nodiscard]] event create_event() noexcept(false);

    private:
        stream_handle handle_ {};

        void destroy() noexcept;
    };

} // namespace kmx::aio::gpu
