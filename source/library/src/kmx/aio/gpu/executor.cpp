/// @file aio/gpu/executor.cpp
/// @brief GPU completion-model executor implementation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/gpu/executor.hpp"
#include "kmx/aio/allocator.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <pthread.h>
#include <queue>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace kmx::aio::gpu
{
    namespace
    {
        thread_local executor* tls_current_gpu_executor = nullptr;
    }

/// CUDA Error Category (Conditional)
#if defined(KMX_AIO_FEATURE_CUDA)
    class cuda_error_category: public std::error_category
    {
    public:
        const char* name() const noexcept override { return "cuda"; }

        std::string message(int ev) const override
        {
            switch (static_cast<::cudaError_t>(ev))
            {
                case cudaSuccess:
                    return "CUDA operation succeeded";
                case cudaErrorMemoryAllocation:
                    return "CUDA out of memory";
                case cudaErrorInitializationError:
                    return "CUDA initialization failed";
                case cudaErrorNotSupported:
                    return "CUDA operation not supported";
                case cudaErrorNotReady:
                    return "CUDA resource not ready";
                default:
                    return "CUDA error code " + std::to_string(ev);
            }
        }
    };

    static const cuda_error_category cuda_category_instance;

    /// @brief Returns the CUDA error category for use in std::system_error.
    inline const std::error_category& cuda_category() noexcept
    {
        return cuda_category_instance;
    }
#endif

    /// Statistics Implementation

    void statistics::reset() noexcept
    {
        total_events_created.store(0u, std::memory_order_relaxed);
        total_events_completed.store(0u, std::memory_order_relaxed);
        total_tasks_spawned.store(0u, std::memory_order_relaxed);
        total_tasks_completed.store(0u, std::memory_order_relaxed);
        error_count.store(0u, std::memory_order_relaxed);
        poll_timeout_count.store(0u, std::memory_order_relaxed);
    }

    /// Executor Implementation

    /// Task Queue (Pending Coroutines)

    class task_queue
    {
    public:
        void enqueue(std::coroutine_handle<> h) noexcept
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.push_back(h);
        }

        std::vector<std::coroutine_handle<>> drain() noexcept
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return std::exchange(pending_, {});
        }

    private:
        std::mutex mutex_;
        std::vector<std::coroutine_handle<>> pending_;
    };

    /// Executor Implementation

    executor::executor(const executor_config& config) noexcept(false): config_(config), stats_()
    {
#if defined(KMX_AIO_FEATURE_CUDA)
        set_gpu_device();
#endif
        // GPU executor initialized successfully.
    }

    executor::~executor() noexcept
    {
        finalize();
    }

    template <typename T>
    void executor::spawn(task<T> coro) noexcept(false)
    {
        active_work_.fetch_add(1u, std::memory_order_acq_rel);
        stats_.total_tasks_spawned.fetch_add(1u, std::memory_order_release);

        const auto self = shared_from_this();
        auto detached = execute_task(std::move(coro), self);

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            pending_tasks_.push_back(detached.handle);
        }

        // Drive progress inline when no dedicated run() loop is active.
        if (!running_.load(std::memory_order_acquire))
        {
            while (active_work_.load(std::memory_order_acquire) > 0u)
            {
                if (!poll_events())
                    std::this_thread::yield();
            }
        }
    }

    void executor::run(std::stop_token stop_token) noexcept(false)
    {
        stop_requested_.store(false, std::memory_order_release);
        running_.store(true, std::memory_order_release);

        // Pin to CPU core if specified (thread-per-core architecture).
        if (config_.core_id >= 0)
        {
#if defined(__linux__)
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(static_cast<int>(config_.core_id), &cpuset);
            const auto ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
            if (ret != 0)
                throw std::system_error(ret, std::generic_category(), "Failed to pin executor to core " + std::to_string(config_.core_id));
#endif
        }

        // GPU executor event loop started.
        while (true)
        {
            const bool had_work = poll_events();

            const bool external_stop = stop_token.stop_requested() || stop_requested_.load(std::memory_order_acquire);
            if (external_stop && !has_pending_work())
                break;

            if (!had_work)
                std::this_thread::yield();
        }

        finalize();
        running_.store(false, std::memory_order_release);
        // GPU executor event loop exited.
    }

    void executor::stop() noexcept
    {
        // Signal the event loop to stop and exit gracefully.
        stop_requested_.store(true, std::memory_order_release);
    }

    const statistics& executor::get_statistics() const noexcept
    {
        return stats_;
    }

    void executor::reset_statistics() noexcept
    {
        stats_.reset();
    }

    void executor::register_waiting_coroutine(const event_handle event, const std::coroutine_handle<> h) noexcept
    {
        if (!event || !h)
            return;

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            waiting_events_[event] = h;
        }

        stats_.total_events_created.fetch_add(1u, std::memory_order_release);
    }

#if defined(KMX_AIO_FEATURE_CUDA)
    void executor::set_gpu_device() noexcept(false)
    {
        // Set the active GPU device for this executor.
        const auto ret_set = ::cudaSetDevice(static_cast<int>(config_.gpu_device));
        if (ret_set != cudaSuccess)
        {
            throw std::system_error(static_cast<int>(ret_set), cuda_category(),
                                    "cudaSetDevice failed for device " + std::to_string(config_.gpu_device));
        }

        // Verify device is usable by querying basic properties.
        int device = -1;
        const auto ret_get = ::cudaGetDevice(&device);
        if (ret_get != cudaSuccess || device != static_cast<int>(config_.gpu_device))
        {
            throw std::system_error(static_cast<int>(ret_get), cuda_category(),
                                    "GPU device " + std::to_string(config_.gpu_device) + " verification failed");
        }
    }
#else
    void executor::set_gpu_device() noexcept(false)
    {
        // Mock: no-op
    }
#endif

    bool executor::poll_events() noexcept
    {
        bool work_done = false;

        // 1. Drain and resume pending tasks from spawn() queue.
        std::deque<std::coroutine_handle<>> pending;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            pending = std::exchange(pending_tasks_, {});
        }

        for (auto handle: pending)
        {
            if (handle)
            {
                tls_current_gpu_executor = this;
                handle.resume();
                tls_current_gpu_executor = nullptr;
                work_done = true;
            }
        }

        // 2. Check waiting events for readiness and resume ready coroutines.
        std::vector<void*> completed_events;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            for (auto it = waiting_events_.begin(); it != waiting_events_.end(); ++it)
            {
                auto event_handle = it->first;
                auto coro_handle = it->second;

                // Try to query event status (non-blocking).
                try
                {
                    bool ready = false;
#if defined(KMX_AIO_FEATURE_CUDA)
                    const auto ret = ::cudaEventQuery(static_cast<::cudaEvent_t>(event_handle));
                    if (ret == cudaSuccess)
                        ready = true;
                    else if (ret != cudaErrorNotReady)
                        throw std::system_error(static_cast<int>(ret), cuda_category(), "cudaEventQuery failed");
#else
                    ready = true;
#endif

                    if (ready)
                    {
                        completed_events.push_back(event_handle);
                        if (coro_handle)
                        {
                            tls_current_gpu_executor = this;
                            coro_handle.resume();
                            tls_current_gpu_executor = nullptr;
                            stats_.total_events_completed.fetch_add(1u, std::memory_order_release);
                            work_done = true;
                        }
                    }
                }
                catch (...)
                {
                    // Silently ignore errors in event polling.
                    stats_.error_count.fetch_add(1u, std::memory_order_relaxed);
                    completed_events.push_back(event_handle);
                }
            }

            // Remove completed events from waiting map.
            for (auto event_handle: completed_events)
                waiting_events_.erase(event_handle);
        }

        return work_done || !completed_events.empty();
    }

    bool executor::has_pending_work() noexcept
    {
        if (active_work_.load(std::memory_order_acquire) > 0u)
            return true;

        std::lock_guard<std::mutex> lock(queue_mutex_);
        return !pending_tasks_.empty() || !waiting_events_.empty();
    }

    void executor::process_events() noexcept
    {
        // Process pending GPU events (called from finalize to drain remaining work).
        const bool had_work = poll_events();
        (void) had_work;
    }

    void executor::finalize() noexcept
    {
        // Drain remaining events and coroutines to ensure clean shutdown.
        // Process any pending work that wasn't drained by run() loop.
        try
        {
            while (poll_events())
            {
                // Keep polling until no more work.
            }
        }
        catch (...)
        {
            // Silently ignore errors during finalization.
        }

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            waiting_events_.clear();
            pending_tasks_.clear();
        }

#if defined(KMX_AIO_FEATURE_CUDA)
        // Destroy GPU resources (future: managed streams, events, etc.)
#endif
    }

    /// Stream Implementation

    stream::stream() noexcept(false)
    {
#if defined(KMX_AIO_FEATURE_CUDA)
        ::cudaStream_t s = nullptr;
        const auto ret = ::cudaStreamCreate(&s);
        if (ret != cudaSuccess)
        {
            throw std::system_error(static_cast<int>(ret), cuda_category(), "cudaStreamCreate failed");
        }
        handle_ = s;
#else
        handle_ = reinterpret_cast<void*>(0xDEADBEEF); // Mock handle (distinctive pattern)
#endif
    }

    stream::~stream() noexcept
    {
        destroy();
    }

    stream::stream(stream&& other) noexcept: handle_(std::exchange(other.handle_, nullptr))
    {
    }

    stream& stream::operator=(stream&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }

        return *this;
    }

    void stream::synchronize() noexcept(false)
    {
#if defined(KMX_AIO_FEATURE_CUDA)
        if (handle_ == nullptr)
            throw std::system_error(static_cast<int>(std::errc::invalid_argument), std::generic_category(), "stream handle is null");

        const auto ret = ::cudaStreamSynchronize(static_cast<::cudaStream_t>(handle_));
        if (ret != cudaSuccess)
            throw std::system_error(static_cast<int>(ret), cuda_category(), "cudaStreamSynchronize failed");
#endif
    }

    event stream::create_event() noexcept(false)
    {
        event e;
#if defined(KMX_AIO_FEATURE_CUDA)
        if (handle_ == nullptr)
        {
            throw std::system_error(static_cast<int>(std::errc::invalid_argument), std::generic_category(), "stream handle is null");
        }
        const auto ret_record = ::cudaEventRecord(static_cast<::cudaEvent_t>(e.handle_), static_cast<::cudaStream_t>(handle_));
        if (ret_record != cudaSuccess)
        {
            throw std::system_error(static_cast<int>(ret_record), cuda_category(), "cudaEventRecord failed");
        }
#else
        e.handle_ = reinterpret_cast<void*>(0xCAFEBABE); // Mock handle (distinctive pattern)
#endif
        return e;
    }

    void stream::destroy() noexcept
    {
        if (handle_ == nullptr)
            return;

#if defined(KMX_AIO_FEATURE_CUDA)
        ::cudaStreamDestroy(static_cast<::cudaStream_t>(handle_));

#endif

        handle_ = nullptr;
    }

    /// Event Implementation

    event::event() noexcept(false)
    {
#if defined(KMX_AIO_FEATURE_CUDA)
        ::cudaEvent_t e = nullptr;
        const auto ret = ::cudaEventCreate(&e);
        if (ret != cudaSuccess)
        {
            throw std::system_error(static_cast<int>(ret), std::generic_category(), "cudaEventCreate failed");
        }
        handle_ = e;
#else
        handle_ = reinterpret_cast<void*>(1); // Mock handle
#endif
    }

    event::~event() noexcept
    {
        destroy();
    }

    event::event(event&& other) noexcept: handle_(std::exchange(other.handle_, nullptr))
    {
    }

    event& event::operator=(event&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }

        return *this;
    }

    event::awaiter event::operator co_await() noexcept
    {
        return awaiter {*this};
    }

    bool event::is_ready() const noexcept(false)
    {
#if defined(KMX_AIO_FEATURE_CUDA)
        if (handle_ == nullptr)
            throw std::system_error(static_cast<int>(std::errc::invalid_argument), std::generic_category(), "event handle is null");

        const auto ret = ::cudaEventQuery(static_cast<::cudaEvent_t>(handle_));
        if (ret == cudaSuccess)
            return true;
        if (ret == cudaErrorNotReady)
            return false;

        throw std::system_error(static_cast<int>(ret), cuda_category(), "cudaEventQuery failed");
#else
        return true; // Mock: always ready
#endif
    }

    bool event::awaiter::await_ready() const noexcept
    {
        // Poll without throwing.
#if defined(KMX_AIO_FEATURE_CUDA)
        const auto ret = ::cudaEventQuery(static_cast<::cudaEvent_t>(event_.handle_));
        return ret == cudaSuccess;
#else
        return true; // Mock
#endif
    }

    void event::awaiter::await_suspend(std::coroutine_handle<> h) noexcept
    {
        auto* const exec = tls_current_gpu_executor;
        if (exec == nullptr)
        {
#if defined(KMX_AIO_FEATURE_CUDA)
            while (true)
            {
                const auto ret = ::cudaEventQuery(static_cast<::cudaEvent_t>(event_.handle_));
                if (ret == cudaSuccess)
                    break;

                if (ret != cudaErrorNotReady)
                    break;

                std::this_thread::yield();
            }
#endif
            h.resume();
            return;
        }

        exec->register_waiting_coroutine(event_.handle_, h);
    }

    void event::destroy() noexcept
    {
        if (handle_ == nullptr)
            return;

#if defined(KMX_AIO_FEATURE_CUDA)
        ::cudaEventDestroy(static_cast<::cudaEvent_t>(handle_));
#endif

        handle_ = nullptr;
    }

    template <typename T>
    executor::detached_task_wrapper executor::execute_task(task<T> t, std::shared_ptr<executor> self) noexcept
    {
        try
        {
            if constexpr (std::is_void_v<T>)
                co_await t;
            else
                (void) co_await t;
        }
        catch (...)
        {
            self->stats_.error_count.fetch_add(1u, std::memory_order_relaxed);
        }

        self->stats_.total_tasks_completed.fetch_add(1u, std::memory_order_release);
        if (self->active_work_.fetch_sub(1u, std::memory_order_acq_rel) == 1u)
            self->idle_cv_.notify_one();
    }

} // namespace kmx::aio::gpu

/// Explicit template instantiation for spawn()
namespace kmx::aio::gpu
{
    template void executor::spawn(task<void> coro) noexcept(false);
    template void executor::spawn(task<int> coro) noexcept(false);
    template executor::detached_task_wrapper executor::execute_task(task<void> t, std::shared_ptr<executor> self) noexcept;
    template executor::detached_task_wrapper executor::execute_task(task<int> t, std::shared_ptr<executor> self) noexcept;
} // namespace kmx::aio::gpu
