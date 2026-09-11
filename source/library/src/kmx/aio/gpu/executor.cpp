/// @file src/kmx/aio/gpu/executor.cpp
/// @brief GPU completion-model executor implementation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/gpu/executor.hpp>
#ifndef PCH
    #include <kmx/aio/allocator/slab.hpp>
    #include <kmx/aio/gpu/detail/cuda_category.hpp>
    #include <kmx/aio/gpu/detail/current_executor.hpp>
    #include <kmx/aio/gpu/event.hpp>
    #include <kmx/aio/system_error.hpp>

    #include <algorithm>
    #include <atomic>
    #include <condition_variable>
    #include <deque>
    #include <mutex>
    #include <queue>
    #include <stop_token>
    #include <string>
    #include <system_error>
    #include <thread>
    #include <type_traits>
    #include <utility>
    #include <vector>
    #include <pthread.h>
    #include <sched.h>
#endif

namespace kmx::aio::gpu
{
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
            while (active_work_.load(std::memory_order_acquire) > 0u)
                if (!poll_events())
                    std::this_thread::yield();
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
                throw system_error(ret, std::generic_category(), "Failed to pin executor to core " + std::to_string(config_.core_id));
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

    void executor::register_waiting_coroutine(const event_handle event, const coroutine_handle_t handle) noexcept
    {
        if (!event || !handle)
            return;

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            waiting_events_[event] = handle;
        }

        stats_.total_events_created.fetch_add(1u, std::memory_order_release);
    }

#if defined(KMX_AIO_FEATURE_CUDA)
    void executor::set_gpu_device() noexcept(false)
    {
        // Set the active GPU device for this executor.
        const auto ret_set = ::cudaSetDevice(static_cast<int>(config_.gpu_device));
        if (ret_set != cudaSuccess)
            throw system_error(static_cast<int>(ret_set), detail::cuda_category(),
                               "cudaSetDevice failed for device " + std::to_string(config_.gpu_device));

        // Verify device is usable by querying basic properties.
        int device = -1;
        const auto ret_get = ::cudaGetDevice(&device);
        if ((ret_get != cudaSuccess) || (device != static_cast<int>(config_.gpu_device)))
            throw system_error(static_cast<int>(ret_get), detail::cuda_category(),
                               "GPU device " + std::to_string(config_.gpu_device) + " verification failed");
    }
#else
    void executor::set_gpu_device() noexcept(false)
    {
        // Mock: no-op
    }
#endif

    void executor::resume_on_executor(const coroutine_handle_t handle) noexcept
    {
        auto* const previous = detail::current_executor;
        detail::current_executor = this;
        handle.resume();
        detail::current_executor = previous;
    }

    bool executor::poll_events() noexcept
    {
        bool work_done {};

        // 1. Drain and resume pending tasks from spawn() queue.
        std::deque<coroutine_handle_t> pending;
        {
            const std::lock_guard lock(queue_mutex_);
            pending = std::exchange(pending_tasks_, {});
        }

        for (const auto handle: pending)
            if (handle)
            {
                resume_on_executor(handle);
                work_done = true;
            }

        // 2. Collect the events that have fired and retire them, then resume their coroutines with the
        //    lock released.
        //
        //    Resuming under the lock is what this arrangement exists to avoid. A resumed coroutine runs
        //    application code, and the two things such code most naturally does next both take
        //    queue_mutex_: awaiting another GPU event reaches register_waiting_coroutine(), and starting
        //    more work reaches spawn(). Either one locks a non-recursive mutex the resuming thread
        //    already holds, which is a deadlock - and the first of them is simply what a coroutine
        //    awaiting two events in sequence does, not an unusual case.
        //
        //    Even where it did not deadlock it was undefined: register_waiting_coroutine() inserts into
        //    waiting_events_, and an insert that rehashes invalidates the iterator the loop was about to
        //    advance.
        //
        //    Retiring an entry before its coroutine runs also settles what happens when that coroutine
        //    waits on the same event handle again - a real possibility, because CUDA reuses a destroyed
        //    event's address. The new registration is a fresh entry made after this one is gone, rather
        //    than something a later erase would silently delete.
        std::vector<coroutine_handle_t> ready_handles;
        bool retired_any {};
        {
            const std::lock_guard lock(queue_mutex_);
            for (auto it = waiting_events_.begin(); it != waiting_events_.end();)
            {
                bool ready {};

                // Try to query event status (non-blocking).
                try
                {
#if defined(KMX_AIO_FEATURE_CUDA)
                    const auto ret = ::cudaEventQuery(static_cast<::cudaEvent_t>(it->first));
                    if (ret == cudaSuccess)
                        ready = true;
                    else if (ret != cudaErrorNotReady)
                        throw system_error(static_cast<int>(ret), detail::cuda_category(), "cudaEventQuery failed");
#else
                    ready = true;
#endif
                }
                catch (...)
                {
                    // Silently ignore errors in event polling; the entry is dropped, as before.
                    stats_.error_count.fetch_add(1u, std::memory_order_relaxed);
                    it = waiting_events_.erase(it);
                    retired_any = true;
                    continue;
                }

                if (!ready)
                {
                    ++it;
                    continue;
                }

                if (it->second)
                    ready_handles.push_back(it->second);

                it = waiting_events_.erase(it);
                retired_any = true;
            }
        }

        for (const auto handle: ready_handles)
        {
            resume_on_executor(handle);
            stats_.total_events_completed.fetch_add(1u, std::memory_order_release);
            work_done = true;
        }

        return work_done || retired_any;
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
        static_cast<void>(had_work);
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

    template <typename T>
    executor::detached_task_wrapper executor::execute_task(task<T> t, std::shared_ptr<executor> self) noexcept
    {
        try
        {
            if constexpr (std::is_void_v<T>)
                co_await t;
            else
                static_cast<void>(co_await t);
        }
        catch (...)
        {
            self->stats_.error_count.fetch_add(1u, std::memory_order_relaxed);
        }

        self->stats_.total_tasks_completed.fetch_add(1u, std::memory_order_release);
        if (self->active_work_.fetch_sub(1u, std::memory_order_acq_rel) == 1u)
        {
            // Under idle_mutex_ for the same reason as in stop(): run()'s predicate reads active_work_,
            // and a notification that lands between its evaluation and the wait is lost.
            {
                const std::lock_guard idle_lock(self->idle_mutex_);
            }
            self->idle_cv_.notify_one();
        }
    }

}

/// Explicit template instantiation for spawn()
namespace kmx::aio::gpu
{
    template void executor::spawn(task<void> coro) noexcept(false);
    template void executor::spawn(task<int> coro) noexcept(false);
    template executor::detached_task_wrapper executor::execute_task(task<void> t, std::shared_ptr<executor> self) noexcept;
    template executor::detached_task_wrapper executor::execute_task(task<int> t, std::shared_ptr<executor> self) noexcept;
}
