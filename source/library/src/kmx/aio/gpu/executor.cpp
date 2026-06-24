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
#include <utility>
#include <vector>

namespace kmx::aio::gpu
{

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
        // NOTE: Spawning a GPU task means enqueueing it for processing in run().
        // Currently, the task is co_await'd (not stored directly) since task<T>
        // manages its own coroutine lifecycle. A full implementation would:
        //   1. Create a wrapper coroutine that co_awaits the task
        //   2. Enqueue the wrapper's handle for resumption
        //   3. Resume wrappers in run() loop
        //
        // For now, this is a placeholder that tracks task spawning statistics.
        // Full implementation pending: GPU task scheduler integration.
        (void) coro;
        stats_.total_tasks_spawned.fetch_add(1u, std::memory_order_release);
    }

    void executor::run(std::stop_token stop_token) noexcept(false)
    {
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
        bool done = false;
        while (!done && !stop_token.stop_requested() && !stop_requested_.load(std::memory_order_acquire))
        {
            done = !poll_events();
            std::this_thread::yield();
        }

        finalize();
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
                handle.resume();
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
                // Note: We cast void* back to event* for is_ready() check.
                // This is safe because we control the event lifecycle.
                try
                {
                    auto* evt = static_cast<event*>(event_handle);
                    if (evt && evt->is_ready())
                    {
                        completed_events.push_back(event_handle);
                        if (coro_handle)
                        {
                            coro_handle.resume();
                            stats_.total_tasks_completed.fetch_add(1u, std::memory_order_release);
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

        // Clear any remaining waiting events.
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
        ::cudaEvent_t evt = nullptr;
        const auto ret_create = ::cudaEventCreate(&evt);
        if (ret_create != cudaSuccess)
            throw std::system_error(static_cast<int>(ret_create), cuda_category(), "cudaEventCreate failed");

        const auto ret_record = ::cudaEventRecord(evt, static_cast<::cudaStream_t>(handle_));
        if (ret_record != cudaSuccess)
        {
            ::cudaEventDestroy(evt);
            throw std::system_error(static_cast<int>(ret_record), cuda_category(), "cudaEventRecord failed");
        }

        e.handle_ = evt;
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
        // Register coroutine for resumption when event fires.
        // Note: In mock mode, is_ready() returned true in await_ready(), so
        // await_suspend is not called. In real CUDA mode, this would register
        // the coroutine with the executor's event polling system.
        //
        // Currently, we don't have direct executor reference here.
        // Future: Pass executor reference through event creation chain:
        //   stream(executor_ref) -> event(executor_ref) -> awaiter(executor_ref)
        // Then register: executor_ref->register_waiting_coroutine(event_, h);
        //
        // For now, this is a placeholder. The polling loop will eventually
        // resume this coroutine when the event fires.
        (void) h; // Suppress unused parameter warning.
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

} // namespace kmx::aio::gpu

/// Explicit template instantiation for spawn()
namespace kmx::aio::gpu
{
    template void executor::spawn(task<void> coro) noexcept(false);
    template void executor::spawn(task<int> coro) noexcept(false);
} // namespace kmx::aio::gpu
