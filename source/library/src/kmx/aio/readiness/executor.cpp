/// @file aio/readiness/executor.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/readiness/executor.hpp"

#include "kmx/aio/error_code.hpp"
#include "kmx/aio/readiness/openonload/extensions.hpp"
#include "kmx/logger.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#include <string_view>
#include <thread>

namespace kmx::aio::readiness
{
#if defined(KMX_AIO_FEATURE_OPENONLOAD)
    [[nodiscard]] static bool env_var_contains(const char* name, const std::string_view token) noexcept
    {
        const char* value = std::getenv(name);
        if (!value)
            return false;

        return std::string_view(value).find(token) != std::string_view::npos;
    }

    [[nodiscard]] static bool is_openonload_runtime_available() noexcept
    {
        // OpenOnload is transparently injected; detect common runtime hints.
        if (env_var_contains("LD_PRELOAD", "onload"))
            return true;

        if (std::getenv("ONLOAD_STACKNAME"))
            return true;

        if (std::getenv("EF_POLL_USEC"))
            return true;

        return false;
#else
    [[nodiscard]] static constexpr bool is_openonload_runtime_available() noexcept
    {
        return false;
#endif
    }

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
        const bool openonload_available = is_openonload_runtime_available();

        switch (config_.backend)
        {
            case backend_mode::epoll_only:
                active_backend_ = active_backend::epoll;
                break;

            case backend_mode::openonload_preferred:
                if (openonload_available)
                    active_backend_ = active_backend::openonload;
                else
                    active_backend_ = active_backend::epoll;
                break;

            case backend_mode::openonload_required:
                if (!openonload_available)
                    throw std::system_error(to_std_error_code(error_code::openonload_not_available),
                                            "OpenOnload backend required but runtime was not detected");

                active_backend_ = active_backend::openonload;
                break;
        }

        if (active_backend_ == active_backend::openonload)
        {
            logger::log(logger::level::info, std::source_location::current(), "Readiness executor backend: OpenOnload");
            openonload::initialize_runtime_stack("kmxaio_fast_stack");
        }
        else
            logger::log(logger::level::info, std::source_location::current(), "Readiness executor backend: epoll");

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
            metrics_.error_count.fetch_add(1u, mem_order);

        {
            const std::lock_guard lock(subscribers_mutex_);
            std::erase_if(subscribers_, [fd](const auto& item) noexcept { return item.first.fd == fd; });
        }
    }

    void executor::subscribe(const fd_t fd, const event_type type, std::coroutine_handle<> handle) noexcept(false)
    {
        const std::lock_guard lock(subscribers_mutex_);
        // operator[] might throw std::bad_alloc
        subscribers_[{fd, type}].push_back(handle);
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
        const auto initial_work = active_work_.load(mem_order);
        if (!running_.exchange(true, std::memory_order_acq_rel))
        {
            const std::lock_guard lock(io_thread_mutex_);
            io_thread_ = std::jthread([this](std::stop_token st) { process_events(st); });
        }

        std::unique_lock lock(idle_mutex_);
        idle_cv_.wait(lock, [this, initial_work]
                      { return !running_.load(mem_order) || ((initial_work > 0u) && (active_work_.load(mem_order) == 0u)); });

        if (running_.load(mem_order))
            stop();

        // If stop() was called from the I/O thread itself, join may be deferred.
        std::jthread thread_to_join;
        {
            const std::lock_guard thread_lock(io_thread_mutex_);
            if (io_thread_.joinable() && (io_thread_.get_id() != std::this_thread::get_id()))
                thread_to_join = std::move(io_thread_);
        }

        if (thread_to_join.joinable())
            thread_to_join.join();
    }

    void executor::stop() noexcept
    {
        if (running_.exchange(false, std::memory_order_acq_rel))
        {
            idle_cv_.notify_all();
            std::jthread thread_to_join;
            {
                const std::lock_guard lock(io_thread_mutex_);
                if (io_thread_.joinable())
                {
                    io_thread_.request_stop();
                    // A timeout in epoll_wait will eventually unblock it.
                    // For immediate unblocking, a pipe-to-self or eventfd would be needed.

                    // Avoid joining the current thread. This can occur when a task
                    // resumed by this executor requests stop().
                    if (io_thread_.get_id() == std::this_thread::get_id())
                        return;

                    thread_to_join = std::move(io_thread_);
                }
            }

            if (thread_to_join.joinable())
                thread_to_join.join();

            return;
        }

        // Allow external completion of deferred join.
        std::jthread thread_to_join;
        {
            const std::lock_guard lock(io_thread_mutex_);
            if (io_thread_.joinable() && (io_thread_.get_id() != std::this_thread::get_id()))
                thread_to_join = std::move(io_thread_);
        }

        if (thread_to_join.joinable())
            thread_to_join.join();
    }

    std::expected<bool, std::error_code> executor::is_io_thread_affined_to(const int core_id) noexcept
    {
        if (core_id < 0)
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));

        cpu_set_t cpuset {};
        CPU_ZERO(&cpuset);

        int ret {};
        {
            const std::lock_guard lock(io_thread_mutex_);
            if (!io_thread_.joinable())
                return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));

            ret = ::pthread_getaffinity_np(io_thread_.native_handle(), sizeof(cpu_set_t), &cpuset);
        }

        if (ret != 0)
            return std::unexpected(std::error_code(ret, std::generic_category()));

        return CPU_ISSET(core_id, &cpuset) != 0;
    }

    void executor::resume_if_found(const fd_t fd, const event_type type)
    {
        std::coroutine_handle<> handle {};
        {
            const std::lock_guard lock(subscribers_mutex_);
            const auto it = subscribers_.find({fd, type});
            if (it != subscribers_.end() && !it->second.empty())
            {
                handle = it->second.front();
                it->second.pop_front();
                if (it->second.empty())
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

        pin_to_core();

        for (std::vector<epoll_event> events;;)
        {
            metrics_.total_epoll_waits.fetch_add(1u, mem_order);
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
                metrics_.timeout_count.fetch_add(1u, mem_order);

            if (st.stop_requested())
                break;
        }
    }

    void executor::pin_to_core() const noexcept
    {
        if (config_.core_id < 0)
            return;

        cpu_set_t cpuset {};
        CPU_ZERO(&cpuset);
        CPU_SET(static_cast<int>(config_.core_id), &cpuset);

        const int ret = ::pthread_setaffinity_np(::pthread_self(), sizeof(cpu_set_t), &cpuset);
        if (ret != 0)
            logger::log(logger::level::warn, std::source_location::current(), "Failed to pin readiness thread to core {}: {}",
                        config_.core_id, std::strerror(ret));
        else
            logger::log(logger::level::info, std::source_location::current(), "Readiness executor pinned to CPU core {}", config_.core_id);
    }

} // namespace kmx::aio::readiness
