#include "kmx/aio/scheduler.hpp"

#include "kmx/logger.hpp"

namespace kmx::aio
{
    scheduler::scheduler(std::uint32_t thread_count) noexcept(false)
    {
        workers_.reserve(thread_count);
        for (; thread_count > 0u; --thread_count)
            workers_.emplace_back([this](std::stop_token st) noexcept { run_worker(st); });
    }

    scheduler::~scheduler() noexcept
    {
        for (auto& worker: workers_)
            worker.request_stop();

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

    void scheduler::run_worker(std::stop_token st) noexcept
    {
        while (!st.stop_requested())
        {
            std::unique_lock lock(queue_mutex_);
            cv_.wait(lock, [this, &st]() noexcept { return !queue_.empty() || st.stop_requested(); });

            if (queue_.empty())
                continue;

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
        }
    }
} // namespace kmx::aio
