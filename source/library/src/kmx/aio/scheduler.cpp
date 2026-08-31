/// @file aio/scheduler.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/scheduler.hpp"

#include "kmx/logger.hpp"

#include <thread>

namespace kmx::aio
{
    scheduler::scheduler(std::uint32_t thread_count) noexcept(false)
    {
        workers_.reserve(thread_count);
        for (; thread_count > 0u; --thread_count)
            workers_.emplace_back([this](std::stop_token token) noexcept { run_worker(token); });
    }

    scheduler::~scheduler() noexcept
    {
        // Move workers to a local so they are joined before synchronization
        // primitives (cv_/queue_mutex_) are destroyed with the object.
        auto workers = std::move(workers_);

        // request_stop() under the queue lock, not outside it. A worker evaluates
        //
        //     !queue_.empty() || token.stop_requested()
        //
        // while holding this mutex, and then releases it and blocks inside cv_.wait(). Requesting the
        // stop without the lock lets the whole change land in the gap between those two steps: the
        // worker has already decided to sleep, the notify_all() below finds nobody registered, and the
        // worker waits for a queue entry that will never arrive. The join at the end of this scope then
        // waits for it forever.
        //
        // Holding the lock closes the gap. Either the worker has not evaluated the predicate yet, in
        // which case it sees the stop; or it is already inside cv_.wait() with the mutex released, in
        // which case the notify reaches it. The stop request is a change to the predicate, so it belongs
        // under the mutex that guards it, exactly as the queue push in spawn() does.
        {
            const std::lock_guard lock(queue_mutex_);
            for (auto& worker: workers)
                worker.request_stop();
        }

        cv_.notify_all();
    }

    void scheduler::spawn(std::move_only_function<void()>&& task) noexcept(false)
    {
        {
            const std::lock_guard lock(queue_mutex_);
            queue_.push_back(std::move(task));
        }

        cv_.notify_one();
    }

    bool scheduler::is_worker_thread() const noexcept
    {
        const auto self = std::this_thread::get_id();
        for (const auto& worker: workers_)
            if (worker.get_id() == self)
                return true;

        return false;
    }

    void scheduler::wait_until_idle() noexcept
    {
        // A worker waiting here would be waiting for itself: the task it is running is exactly what
        // keeps the scheduler busy, and it cannot finish until this returns.
        if (is_worker_thread())
            return;

        std::unique_lock lock(queue_mutex_);
        idle_cv_.wait(lock, [this]() noexcept { return queue_.empty() && (active_ == 0u); });
    }

    void scheduler::run_worker(std::stop_token token) noexcept
    {
        while (!token.stop_requested())
        {
            std::unique_lock lock(queue_mutex_);
            cv_.wait(lock, [this, &token]() noexcept { return !queue_.empty() || token.stop_requested(); });

            if (queue_.empty())
                continue;

            ++active_;

            {
                // Move the task out to execute it without holding the lock
                std::move_only_function<void()> task {std::move(queue_.front())};
                queue_.pop_front();
                lock.unlock();

                try
                {
                    task();
                }
                catch (const std::exception& e)
                {
                    logger::log(logger::level::error, std::source_location::current(), "Exception in scheduler worker: {}", e.what());
                }

                // task goes out of scope here, before the count below is dropped. That order is the
                // whole point: whatever the task captured - including, at times, the last reference to
                // the object that owns this scheduler - has to be released while the scheduler still
                // reports itself busy, or wait_until_idle() returns to an owner that is not yet safe to
                // destroy.
            }

            lock.lock();
            --active_;
            if (queue_.empty() && (active_ == 0u))
            {
                lock.unlock();
                idle_cv_.notify_all();
            }
        }

        // A worker leaving on a stop request must not leave wait_until_idle() waiting on a count only it
        // could have dropped.
        {
            const std::lock_guard lock(queue_mutex_);
            if (queue_.empty() && (active_ == 0u))
            {
                // notify outside is not possible here without another unlock dance; notifying under the
                // lock is correct, only marginally less efficient.
                idle_cv_.notify_all();
            }
        }
    }
} // namespace kmx::aio
