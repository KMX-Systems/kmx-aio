/// @file aio/completion/executor.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/completion/executor.hpp"

#include "kmx/aio/allocator.hpp"
#include "kmx/logger.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <pthread.h>
#include <sched.h>

namespace kmx::aio::completion
{
    static constexpr auto mem_order = std::memory_order_relaxed;

    void statistics::reset() noexcept
    {
        total_submissions.store(0u, mem_order);
        total_completions.store(0u, mem_order);
        total_tasks_spawned.store(0u, mem_order);
        total_tasks_completed.store(0u, mem_order);
        error_count.store(0u, mem_order);
        submission_full_count.store(0u, mem_order);
    }

    executor::executor(const executor_config& config) noexcept(false):
        config_(config)
    {
        const int ret = ::io_uring_queue_init(config.ring_entries, &ring_, 0);
        if (ret < 0)
            throw std::system_error(-ret, std::generic_category(), "io_uring_queue_init failed");
    }

    executor::~executor() noexcept
    {
        stop();
        ::io_uring_queue_exit(&ring_);
    }

    task<std::expected<std::size_t, std::error_code>>
        executor::async_read(const fd_t fd, std::span<char> buffer, const std::uint64_t offset) noexcept(false)
    {
        io_context ctx {};

        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
        {
            metrics_.submission_full_count.fetch_add(1u, mem_order);
            co_return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        }

        ::io_uring_prep_read(sqe, fd, buffer.data(), static_cast<unsigned int>(buffer.size()), offset);
        ::io_uring_sqe_set_data(sqe, &ctx);

        if (const auto sub = submit(); !sub)
            co_return std::unexpected(sub.error());

        metrics_.total_submissions.fetch_add(1u, mem_order);

        // Suspend until the kernel delivers the CQE
        co_await io_awaiter {ctx};

        if (ctx.result < 0)
            co_return std::unexpected(std::error_code(-ctx.result, std::generic_category()));

        co_return static_cast<std::size_t>(ctx.result);
    }

    task<std::expected<std::size_t, std::error_code>>
        executor::async_write(const fd_t fd, std::span<const char> buffer, const std::uint64_t offset) noexcept(false)
    {
        io_context ctx {};

        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
        {
            metrics_.submission_full_count.fetch_add(1u, mem_order);
            co_return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        }

        ::io_uring_prep_write(sqe, fd, buffer.data(), static_cast<unsigned int>(buffer.size()), offset);
        ::io_uring_sqe_set_data(sqe, &ctx);

        if (const auto sub = submit(); !sub)
            co_return std::unexpected(sub.error());

        metrics_.total_submissions.fetch_add(1u, mem_order);

        co_await io_awaiter {ctx};

        if (ctx.result < 0)
            co_return std::unexpected(std::error_code(-ctx.result, std::generic_category()));

        co_return static_cast<std::size_t>(ctx.result);
    }

    std::expected<void, std::error_code> executor::register_buffers(std::span<const ::iovec> iovecs) noexcept
    {
        const int ret = ::io_uring_register_buffers(&ring_, iovecs.data(), static_cast<unsigned int>(iovecs.size()));
        if (ret < 0)
            return std::unexpected(std::error_code(-ret, std::generic_category()));

        return std::expected<void, std::error_code> {};
    }

    std::expected<void, std::error_code> executor::unregister_buffers() noexcept
    {
        const int ret = ::io_uring_unregister_buffers(&ring_);
        if (ret < 0)
            return std::unexpected(std::error_code(-ret, std::generic_category()));

        return std::expected<void, std::error_code> {};
    }

    task<std::expected<std::size_t, std::error_code>>
        executor::async_read_fixed(const fd_t fd, std::span<char> buffer, const std::uint64_t offset, const int buf_index) noexcept(false)
    {
        io_context ctx {};

        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
        {
            metrics_.submission_full_count.fetch_add(1u, mem_order);
            co_return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        }

        ::io_uring_prep_read_fixed(sqe, fd, buffer.data(), static_cast<unsigned int>(buffer.size()), offset, buf_index);
        ::io_uring_sqe_set_data(sqe, &ctx);

        if (const auto sub = submit(); !sub)
            co_return std::unexpected(sub.error());

        metrics_.total_submissions.fetch_add(1u, mem_order);

        co_await io_awaiter {ctx};

        if (ctx.result < 0)
            co_return std::unexpected(std::error_code(-ctx.result, std::generic_category()));

        co_return static_cast<std::size_t>(ctx.result);
    }

    task<std::expected<std::size_t, std::error_code>>
        executor::async_write_fixed(const fd_t fd, std::span<const char> buffer, const std::uint64_t offset, const int buf_index) noexcept(false)
    {
        io_context ctx {};

        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
        {
            metrics_.submission_full_count.fetch_add(1u, mem_order);
            co_return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        }

        ::io_uring_prep_write_fixed(sqe, fd, buffer.data(), static_cast<unsigned int>(buffer.size()), offset, buf_index);
        ::io_uring_sqe_set_data(sqe, &ctx);

        if (const auto sub = submit(); !sub)
            co_return std::unexpected(sub.error());

        metrics_.total_submissions.fetch_add(1u, mem_order);

        co_await io_awaiter {ctx};

        if (ctx.result < 0)
            co_return std::unexpected(std::error_code(-ctx.result, std::generic_category()));

        co_return static_cast<std::size_t>(ctx.result);
    }

    task<std::expected<fd_t, std::error_code>>
        executor::async_accept(const fd_t listen_fd, sockaddr_storage& addr, socklen_t& addrlen) noexcept(false)
    {
        io_context ctx {};

        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
        {
            metrics_.submission_full_count.fetch_add(1u, mem_order);
            co_return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        }

        addrlen = sizeof(sockaddr_storage);
        ::io_uring_prep_accept(sqe, listen_fd, reinterpret_cast<sockaddr*>(&addr), &addrlen, 0);
        ::io_uring_sqe_set_data(sqe, &ctx);

        if (const auto sub = submit(); !sub)
            co_return std::unexpected(sub.error());

        metrics_.total_submissions.fetch_add(1u, mem_order);

        co_await io_awaiter {ctx};

        if (ctx.result < 0)
            co_return std::unexpected(std::error_code(-ctx.result, std::generic_category()));

        co_return ctx.result;
    }

    task<std::expected<void, std::error_code>>
        executor::async_connect(const fd_t fd, const sockaddr* addr, const socklen_t addrlen) noexcept(false)
    {
        io_context ctx {};

        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
        {
            metrics_.submission_full_count.fetch_add(1u, mem_order);
            co_return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        }

        ::io_uring_prep_connect(sqe, fd, addr, addrlen);
        ::io_uring_sqe_set_data(sqe, &ctx);

        if (const auto sub = submit(); !sub)
            co_return std::unexpected(sub.error());

        metrics_.total_submissions.fetch_add(1u, mem_order);

        co_await io_awaiter {ctx};

        if (ctx.result < 0)
            co_return std::unexpected(std::error_code(-ctx.result, std::generic_category()));

        co_return std::expected<void, std::error_code> {};
    }

    task<std::expected<std::size_t, std::error_code>>
        executor::async_recvmsg(const fd_t fd, msghdr* msg, const unsigned int flags) noexcept(false)
    {
        io_context ctx {};

        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
        {
            metrics_.submission_full_count.fetch_add(1u, mem_order);
            co_return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        }

        ::io_uring_prep_recvmsg(sqe, fd, msg, flags);
        ::io_uring_sqe_set_data(sqe, &ctx);

        if (const auto sub = submit(); !sub)
            co_return std::unexpected(sub.error());

        metrics_.total_submissions.fetch_add(1u, mem_order);

        co_await io_awaiter {ctx};

        if (ctx.result < 0)
            co_return std::unexpected(std::error_code(-ctx.result, std::generic_category()));

        co_return static_cast<std::size_t>(ctx.result);
    }

    task<std::expected<std::size_t, std::error_code>>
        executor::async_sendmsg(const fd_t fd, const msghdr* msg, const unsigned int flags) noexcept(false)
    {
        io_context ctx {};

        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
        {
            metrics_.submission_full_count.fetch_add(1u, mem_order);
            co_return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        }

        ::io_uring_prep_sendmsg(sqe, fd, msg, flags);
        ::io_uring_sqe_set_data(sqe, &ctx);

        if (const auto sub = submit(); !sub)
            co_return std::unexpected(sub.error());

        metrics_.total_submissions.fetch_add(1u, mem_order);

        co_await io_awaiter {ctx};

        if (ctx.result < 0)
            co_return std::unexpected(std::error_code(-ctx.result, std::generic_category()));

        co_return static_cast<std::size_t>(ctx.result);
    }

    task<std::expected<void, std::error_code>>
        executor::async_cancel(const std::uint64_t user_data) noexcept(false)
    {
        io_context ctx {};

        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
        {
            metrics_.submission_full_count.fetch_add(1u, mem_order);
            co_return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        }

        ::io_uring_prep_cancel64(sqe, user_data, 0);
        ::io_uring_sqe_set_data(sqe, &ctx);

        if (const auto sub = submit(); !sub)
            co_return std::unexpected(sub.error());

        metrics_.total_submissions.fetch_add(1u, mem_order);

        co_await io_awaiter {ctx};

        if ((ctx.result < 0) && (ctx.result != -EALREADY) && (ctx.result != -ENOENT))
            co_return std::unexpected(std::error_code(-ctx.result, std::generic_category()));

        co_return std::expected<void, std::error_code> {};
    }

    void executor::spawn(task<void>&& t) noexcept(false)
    {
        active_work_.fetch_add(1u, mem_order);
        metrics_.total_tasks_spawned.fetch_add(1u, mem_order);
        auto self = shared_from_this();

        const auto dt = execute_task(std::move(t), std::move(self));
        dt.handle.resume();
    }

    void executor::run() noexcept(false)
    {
        if (!running_.exchange(true, std::memory_order_acq_rel))
            io_thread_ = std::jthread([this](std::stop_token st) { event_loop(st); });

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
                io_thread_.join();
            }
        }
    }

    std::expected<std::size_t, std::error_code> executor::submit() noexcept
    {
        const int ret = ::io_uring_submit(&ring_);
        if (ret < 0)
        {
            metrics_.error_count.fetch_add(1u, mem_order);
            return std::unexpected(std::error_code(-ret, std::generic_category()));
        }

        return static_cast<std::size_t>(ret);
    }

    void executor::process_completions() noexcept
    {
        ::io_uring_cqe* cqe {};

        while (::io_uring_peek_cqe(&ring_, &cqe) == 0)
        {
            metrics_.total_completions.fetch_add(1u, mem_order);

            auto* ctx = static_cast<io_context*>(::io_uring_cqe_get_data(cqe));
            if (ctx != nullptr)
            {
                ctx->result = cqe->res;
                if (ctx->continuation)
                    ctx->continuation.resume();
            }

            ::io_uring_cqe_seen(&ring_, cqe);
        }
    }

    void executor::event_loop(std::stop_token st) noexcept
    {
        pin_to_core();

        // Initialize coroutine slab allocator for this event loop thread.
        // E.g. allocating 1024-byte frames for up to the maximum ring entries.
        // Adjust sizes according to actual task frame requirements.
        slab_allocator coro_allocator {1024u, std::max(1024u, config_.ring_entries * 4u)};
        set_thread_allocator(&coro_allocator);

        while (!st.stop_requested())
        {
            ::io_uring_cqe* cqe {};
            __kernel_timespec ts {};
            ts.tv_sec = 0;
            ts.tv_nsec = 100'000'000; // 100ms timeout

            const int ret = ::io_uring_wait_cqe_timeout(&ring_, &cqe, &ts);
            if (ret == 0)
                process_completions();
            else if ((ret != -ETIME) && (ret != -EINTR))
            {
                metrics_.error_count.fetch_add(1u, mem_order);
                logger::log(logger::level::error, std::source_location::current(),
                            "io_uring_wait_cqe_timeout error: {}", std::strerror(-ret));
            }
        }

        // Drain remaining completions before shutdown
        process_completions();

        set_thread_allocator(nullptr);
    }

    void executor::pin_to_core() const noexcept
    {
        if (config_.core_id < 0)
            return;

        cpu_set_t cpuset {};
        CPU_ZERO(&cpuset);
        CPU_SET(config_.core_id, &cpuset);

        const int ret = ::pthread_setaffinity_np(::pthread_self(), sizeof(cpu_set_t), &cpuset);
        if (ret != 0)
            logger::log(logger::level::warn, std::source_location::current(),
                        "Failed to pin io_uring thread to core {}: {}", config_.core_id, std::strerror(ret));
        else
            logger::log(logger::level::info, std::source_location::current(),
                        "io_uring executor pinned to CPU core {}", config_.core_id);
    }

    executor::detached_task_wrapper executor::execute_task(task<void> tsk, std::shared_ptr<executor> self) noexcept
    {
        try
        {
            co_await tsk;
        }
        catch (const std::exception& e)
        {
            logger::log(logger::level::error, std::source_location::current(),
                        "Exception propagated to top-level completion task: {}", e.what());
        }

        self->metrics_.total_tasks_completed.fetch_add(1u, mem_order);
        if (self->active_work_.fetch_sub(1u, std::memory_order_acq_rel) == 1u)
            self->idle_cv_.notify_one();
    }

} // namespace kmx::aio::completion
