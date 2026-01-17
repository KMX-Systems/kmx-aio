#include "kmx/aio/executor.hpp"

#include "kmx/logger.hpp"
#include <cerrno>
#include <cstring>

namespace kmx::aio
{
    static constexpr auto mem_order = std::memory_order_relaxed;

    void statistics::reset() noexcept
    {
        total_registrations.store(0u, mem_order);
        total_unregistrations.store(0u, mem_order);
        total_epoll_waits.store(0u, mem_order);
        total_events_received.store(0u, mem_order);
        timeout_count.store(0u, mem_order);
        error_count.store(0u, mem_order);
        total_tasks_spawned.store(0u, mem_order);
        total_tasks_completed.store(0u, mem_order);
    }

    executor::executor(const executor_config& config) noexcept(false):
        config_(config),
        scheduler_(std::make_shared<scheduler>(config.thread_count))
    {
        auto epoll_result = descriptor::epoll::create();
        if (!epoll_result)
            throw std::system_error(epoll_result.error(), "epoll_create1 failed");

        epoll_fd_ = std::move(epoll_result.value());
    }

    executor::~executor() noexcept
    {
        stop();
    }

    std::expected<void, std::error_code> executor::register_fd(const fd_t fd) noexcept
    {
        metrics_.total_registrations.fetch_add(1u, mem_order);
        const auto result = epoll_fd_.add_monitored_fd(fd, default_epoll_events);
        if (!result)
            metrics_.error_count.fetch_add(1u, mem_order);

        return result;
    }

    void executor::unregister_fd(const fd_t fd) noexcept
    {
        metrics_.total_unregistrations.fetch_add(1u, mem_order);

        const auto result = epoll_fd_.remove_monitored_fd(fd);
        if (!result)
        {
            metrics_.error_count.fetch_add(1u, mem_order);
        }

        {
            const std::lock_guard lock(subscribers_mutex_);
            std::erase_if(subscribers_, [fd](const auto& item) noexcept { return item.first.fd == fd; });
        }
    }

    void executor::subscribe(const fd_t fd, const event_type type, std::coroutine_handle<> handle) noexcept(false)
    {
        const std::lock_guard lock(subscribers_mutex_);
        // operator[] might throw std::bad_alloc
        subscribers_[{fd, type}] = handle;
    }

    void executor::spawn(task<void>&& t) noexcept(false)
    {
        active_work_.fetch_add(1u, mem_order);
        metrics_.total_tasks_spawned.fetch_add(1u, mem_order);
        auto self = shared_from_this();

        // Create and execute the detached task
        const auto dt = execute_task(std::move(t), std::move(self));
        const auto handle = dt.handle;

        scheduler_->spawn([handle]() mutable { handle.resume(); });
    }

    executor::detached_task_wrapper executor::execute_task(task<void> tsk, std::shared_ptr<executor> self) noexcept
    {
        try
        {
            co_await tsk;
        }
        catch (const std::exception& e)
        {
            logger::log(logger::level::error, std::source_location::current(), "Exception propagated to top-level task: {}", e.what());
        }

        self->metrics_.total_tasks_completed.fetch_add(1u, mem_order);
        if (self->active_work_.fetch_sub(1u, std::memory_order_acq_rel) == 1u)
            self->idle_cv_.notify_one();
    }

    void executor::run() noexcept(false)
    {
        if (!running_.exchange(true, std::memory_order_acq_rel))
            io_thread_ = std::jthread([this](std::stop_token st) { process_events(st); });

        std::unique_lock lock(idle_mutex_);
        idle_cv_.wait(lock, [this] { return !running_.load(mem_order) || (active_work_.load(mem_order) == 0); });

        if (running_.load(mem_order))
            stop();
    }

    void executor::stop() noexcept
    {
        if (running_.exchange(false, std::memory_order_acq_rel))
        {
            idle_cv_.notify_all();
            if (io_thread_.joinable())
            {
                io_thread_.request_stop();
                // A timeout in epoll_wait will eventually unblock it.
                // For immediate unblocking, a pipe-to-self or eventfd would be needed.
                io_thread_.join();
            }
        }
    }

    void executor::resume_if_found(const fd_t fd, const event_type type)
    {
        std::coroutine_handle<> handle {};
        {
            const std::lock_guard lock(subscribers_mutex_);
            const auto it = subscribers_.find({fd, type});
            if (it != subscribers_.end())
            {
                handle = it->second;
                subscribers_.erase(it);
            }
        }

        if (handle)
        {
            // The resumed coroutine is part of a larger task whose lifetime
            // is already tracked by the wrapper in spawn(). We just need to schedule the resumption.
            // We also hold a shared_ptr to the executor to prevent it from being destroyed
            // while this resumption is in flight.
            auto self = shared_from_this();
            scheduler_->spawn([self, handle]() { handle.resume(); });
        }
    }

    void executor::process_events(std::stop_token st) noexcept(false)
    {
        static constexpr std::uint32_t read_mask = EPOLLIN | EPOLLERR | EPOLLHUP;
        static constexpr std::uint32_t write_mask = EPOLLOUT | EPOLLERR | EPOLLHUP;

        for (std::vector<epoll_event> events;;)
        {
            const auto events_result = epoll_fd_.wait_events(events, config_.max_events, config_.timeout_ms);
            if (!events_result)
            {
                if (events_result.error().value() == EINTR)
                    continue;
                metrics_.error_count.fetch_add(1u, mem_order);
                logger::log(logger::level::error, std::source_location::current(), "epoll_wait error: {}", events_result.error().message());
                break;
            }

            if (!events.empty())
            {
                metrics_.total_epoll_waits.fetch_add(1u, mem_order);
                metrics_.total_events_received.fetch_add(events.size(), mem_order);
                for (const auto& item: events)
                {
                    const auto fd = item.data.fd;
                    const auto new_events = item.events;

                    if ((new_events & read_mask) != 0)
                        resume_if_found(fd, event_type::read);

                    if ((new_events & write_mask) != 0)
                        resume_if_found(fd, event_type::write);
                }
            }
            else
            {
                metrics_.timeout_count.fetch_add(1u, mem_order);
            }

            if (st.stop_requested())
                break;
        }
    }

} // namespace kmx::aio
