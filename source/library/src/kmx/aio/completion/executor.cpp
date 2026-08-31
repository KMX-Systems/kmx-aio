/// @file aio/completion/executor.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/completion/detail/uring_syscalls.hpp>
#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/detail/syscalls.hpp>

#include <kmx/aio/allocator/slab.hpp>
#include <kmx/logger.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#include <thread>

namespace kmx::aio::completion
{
    static constexpr auto mem_order = std::memory_order_relaxed;

    /// @brief The executor whose event loop is running on this thread, if any.
    /// @details Lets submit() tell "the loop will flush this in a moment" from "nobody here is going to
    ///          wait, so it has to go now".
    thread_local const executor* t_current_loop_executor = nullptr;

    void statistics::reset() noexcept
    {
        total_submissions.store(0u, mem_order);
        total_completions.store(0u, mem_order);
        total_tasks_spawned.store(0u, mem_order);
        total_tasks_completed.store(0u, mem_order);
        error_count.store(0u, mem_order);
        submission_full_count.store(0u, mem_order);
    }

    executor::executor(const executor_config& config) noexcept(false): config_(config)
    {
        // Opened with no setup flags. COOP_TASKRUN and TASKRUN_FLAG were tried here - the pairing a
        // single-loop ring is usually advised to use - and made no measurable difference to any case in
        // the benchmark suite, whose completions are produced inline by the kernel rather than by a
        // device interrupt on another core. They are worth revisiting against a real NIC, where the
        // interrupt they suppress is the point. SINGLE_ISSUER and DEFER_TASKRUN are not available at
        // all: both require every submission to come from one thread, and spawn() resumes a task on
        // whichever thread called it.
        const int ret = detail::uring_syscalls::queue_init(config.ring_entries, &ring_, 0u);

        if (ret < 0)
            throw std::system_error(-ret, std::generic_category(), "io_uring_queue_init failed");
    }

    executor::~executor() noexcept
    {
        stop();
        ::io_uring_queue_exit(&ring_);
    }

    task_returning_expected_size_t executor::async_read(const fd_t fd, std::span<char> buffer, const std::uint64_t offset) noexcept(false)
    {
        io_context ctx {};
        auto* const sqe = ::io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
        {
            metrics_.submission_full_count.fetch_add(1u, mem_order);
            co_return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        }

        ::io_uring_prep_read(sqe, fd, buffer.data(), static_cast<unsigned>(buffer.size()), offset);
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

    task_returning_expected_size_t executor::async_write(const fd_t fd, std::span<const char> buffer,
                                                         const std::uint64_t offset) noexcept(false)
    {
        io_context ctx {};
        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
        {
            metrics_.submission_full_count.fetch_add(1u, mem_order);
            co_return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        }

        ::io_uring_prep_write(sqe, fd, buffer.data(), static_cast<unsigned>(buffer.size()), offset);
        ::io_uring_sqe_set_data(sqe, &ctx);

        if (const auto sub = submit(); !sub)
            co_return std::unexpected(sub.error());

        metrics_.total_submissions.fetch_add(1u, mem_order);
        co_await io_awaiter {ctx};
        if (ctx.result < 0)
            co_return std::unexpected(std::error_code(-ctx.result, std::generic_category()));

        co_return static_cast<std::size_t>(ctx.result);
    }

    expected_void_t executor::register_buffers(std::span<const ::iovec> iovecs) noexcept
    {
        const int ret = ::io_uring_register_buffers(&ring_, iovecs.data(), static_cast<unsigned>(iovecs.size()));
        if (ret < 0)
            return std::unexpected(std::error_code(-ret, std::generic_category()));

        return expected_void_t {};
    }

    expected_void_t executor::unregister_buffers() noexcept
    {
        const int ret = ::io_uring_unregister_buffers(&ring_);
        if (ret < 0)
            return std::unexpected(std::error_code(-ret, std::generic_category()));

        return expected_void_t {};
    }

    task_returning_expected_size_t executor::async_read_fixed(const fd_t fd, std::span<char> buffer, const std::uint64_t offset,
                                                              const int buf_index) noexcept(false)
    {
        io_context ctx {};
        auto* const sqe = ::io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
        {
            metrics_.submission_full_count.fetch_add(1u, mem_order);
            co_return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        }

        ::io_uring_prep_read_fixed(sqe, fd, buffer.data(), static_cast<unsigned>(buffer.size()), offset, buf_index);
        ::io_uring_sqe_set_data(sqe, &ctx);
        if (const auto sub = submit(); !sub)
            co_return std::unexpected(sub.error());

        metrics_.total_submissions.fetch_add(1u, mem_order);
        co_await io_awaiter {ctx};
        if (ctx.result < 0)
            co_return std::unexpected(std::error_code(-ctx.result, std::generic_category()));

        co_return static_cast<std::size_t>(ctx.result);
    }

    task_returning_expected_size_t executor::async_write_fixed(const fd_t fd, std::span<const char> buffer, const std::uint64_t offset,
                                                               const int buf_index) noexcept(false)
    {
        io_context ctx {};
        auto* const sqe = ::io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
        {
            metrics_.submission_full_count.fetch_add(1u, mem_order);
            co_return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        }

        ::io_uring_prep_write_fixed(sqe, fd, buffer.data(), static_cast<unsigned>(buffer.size()), offset, buf_index);
        ::io_uring_sqe_set_data(sqe, &ctx);

        if (const auto sub = submit(); !sub)
            co_return std::unexpected(sub.error());

        metrics_.total_submissions.fetch_add(1u, mem_order);
        co_await io_awaiter {ctx};
        if (ctx.result < 0)
            co_return std::unexpected(std::error_code(-ctx.result, std::generic_category()));

        co_return static_cast<std::size_t>(ctx.result);
    }

    task<std::expected<fd_t, std::error_code>> executor::async_accept(const fd_t listen_fd, sockaddr_storage& addr,
                                                                      socklen_t& addrlen) noexcept(false)
    {
        io_context ctx {};
        auto* const sqe = ::io_uring_get_sqe(&ring_);
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

    task_returning_expected_void_t executor::async_connect(const fd_t fd, const sockaddr* addr, const socklen_t addrlen) noexcept(false)
    {
        const auto result =
            co_await await_uring_result([fd, addr, addrlen](auto* sqe, auto&) { ::io_uring_prep_connect(sqe, fd, addr, addrlen); });

        if (!result)
            co_return std::unexpected(result.error());

        co_return expected_void_t {};
    }

    task_returning_expected_size_t executor::async_recvmsg(const fd_t fd, msghdr* msg, const unsigned flags) noexcept(false)
    {
        const auto result =
            co_await await_uring_result([fd, msg, flags](auto* sqe, auto&) { ::io_uring_prep_recvmsg(sqe, fd, msg, flags); });

        if (!result)
            co_return std::unexpected(result.error());

        co_return static_cast<std::size_t>(*result);
    }

    task_returning_expected_size_t executor::async_sendmsg(const fd_t fd, const msghdr* msg, const unsigned flags) noexcept(false)
    {
        const auto result =
            co_await await_uring_result([fd, msg, flags](auto* sqe, auto&) { ::io_uring_prep_sendmsg(sqe, fd, msg, flags); });

        if (!result)
            co_return std::unexpected(result.error());

        co_return static_cast<std::size_t>(*result);
    }

    task_returning_expected_void_t executor::async_cancel(const std::uint64_t user_data) noexcept(false)
    {
        io_context ctx {};
        auto* const sqe = ::io_uring_get_sqe(&ring_);
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

        co_return expected_void_t {};
    }

    task_returning_expected_void_t executor::async_timeout(const std::uint64_t duration_ns) noexcept(false)
    {
        io_context ctx {};
        auto* const sqe = ::io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
        {
            metrics_.submission_full_count.fetch_add(1u, mem_order);
            co_return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        }

        __kernel_timespec ts {
            .tv_sec = static_cast<decltype(__kernel_timespec::tv_sec)>(duration_ns / 1'000'000'000ULL),
            .tv_nsec = static_cast<decltype(__kernel_timespec::tv_nsec)>(duration_ns % 1'000'000'000ULL),
        };

        // **Count zero, not one, and the third argument is a completion count rather than a repeat count.**
        // IORING_OP_TIMEOUT with a non-zero `off` completes when that many CQEs have been posted *or* the
        // duration expires, whichever comes first - so a count of one made this return on the next completion
        // of any unrelated operation instead of sleeping. On an idle ring the difference is invisible; under
        // load the timer fires immediately and repeatedly, which is the opposite of what every caller here
        // asks for. `completion/timer.cpp`, `quic::transport`'s retransmit delay and the AVB samples all want
        // a sleep, and the readiness executor's implementation of this same interface uses a timerfd and
        // provides one. Zero makes the two agree.
        ::io_uring_prep_timeout(sqe, &ts, 0u, 0u);
        ::io_uring_sqe_set_data(sqe, &ctx);

        if (const auto sub = submit(); !sub)
            co_return std::unexpected(sub.error());

        metrics_.total_submissions.fetch_add(1u, mem_order);
        co_await io_awaiter {ctx};

        if ((ctx.result < 0) && (ctx.result != -ETIME))
            co_return std::unexpected(std::error_code(-ctx.result, std::generic_category()));

        co_return expected_void_t {};
    }

    task<std::expected<int, std::error_code>> executor::async_poll(const fd_t fd, const unsigned poll_mask) noexcept(false)
    {
        if (fd < 0)
            co_return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));

        io_context ctx {};
        auto* const sqe = ::io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
        {
            metrics_.submission_full_count.fetch_add(1u, mem_order);
            co_return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        }

        ::io_uring_prep_poll_add(sqe, fd, poll_mask);
        ::io_uring_sqe_set_data(sqe, &ctx);
        if (const auto sub = submit(); !sub)
            co_return std::unexpected(sub.error());

        metrics_.total_submissions.fetch_add(1u, mem_order);
        co_await io_awaiter {ctx};

        // io_uring returns poll revents in ctx.result as a positive bitmask on success.
        if (ctx.result < 0)
            co_return std::unexpected(std::error_code(-ctx.result, std::generic_category()));

        co_return ctx.result;
    }

    void executor::spawn(task<void>&& t) noexcept(false)
    {
        active_work_.fetch_add(1u, mem_order);
        metrics_.total_tasks_spawned.fetch_add(1u, mem_order);

        const auto dt = execute_task(std::move(t), this, get_lifetime_token());
        dt.handle.resume();
    }

    void executor::run() noexcept(false)
    {
        if (!running_.exchange(true, std::memory_order_acq_rel))
        {
            const std::lock_guard thread_lock(io_thread_mutex_);
            io_thread_ = std::jthread([this](std::stop_token st) { event_loop(st); });
        }

        std::unique_lock lock(idle_mutex_);
        idle_cv_.wait(lock, [this] { return !running_.load(mem_order) || (active_work_.load(mem_order) == 0u); });

        // The wait is over; nothing below reads the idle state, and stop() now takes this mutex to
        // publish its change safely. Holding it across that call would be a thread locking a
        // non-recursive mutex it already owns.
        lock.unlock();

        if (running_.load(mem_order))
            stop();

        // If stop() was initiated from the I/O thread itself, join is deferred.
        // Ensure the control thread joins here before returning.
        std::jthread thread_to_join;
        {
            const std::lock_guard thread_lock(io_thread_mutex_);
            if (io_thread_.joinable() && (io_thread_.get_id() != std::this_thread::get_id())) // LCOV_EXCL_BR_LINE
            {
                // request_stop() here and not only in stop(). stop() asks the I/O thread to finish only on
                // the call that wins running_.exchange(false), and run() publishes running_ = true before it
                // takes this mutex to create the thread. A stop landing in that gap therefore finds no
                // thread to ask, and the run() that creates it a moment later sees running_ already false
                // and skips its own stop() - leaving a thread nobody has asked to finish, and this join
                // waiting on it for good. Asking again costs nothing when the stop was already requested.
                io_thread_.request_stop();
                thread_to_join = std::move(io_thread_);
            }
        }

        if (thread_to_join.joinable())
            thread_to_join.join();
    }

    void executor::stop() noexcept
    {
        if (running_.exchange(false, std::memory_order_acq_rel))
        {
            // Under idle_mutex_, not outside it. run() evaluates its wait predicate - which reads
            // running_ and active_work_ - while holding this mutex, and then releases it inside
            // idle_cv_.wait(). A notification issued in the gap between those two steps reaches nobody,
            // and run() then waits for a wake-up that has already been and gone. Taking the mutex first
            // means run() is either not yet at the predicate or already waiting; both are woken.
            {
                const std::lock_guard idle_lock(idle_mutex_);
            }
            idle_cv_.notify_all();
            std::jthread thread_to_join;
            {
                const std::lock_guard thread_lock(io_thread_mutex_);
                if (io_thread_.joinable()) // LCOV_EXCL_BR_LINE
                {
                    io_thread_.request_stop();

                    // Avoid joining the current thread. This can happen if a coroutine
                    // resumed on the I/O loop calls exec->stop().
                    if (io_thread_.get_id() == std::this_thread::get_id())
                        return;

                    thread_to_join = std::move(io_thread_);
                }
            }

            if (thread_to_join.joinable()) // LCOV_EXCL_BR_LINE
                thread_to_join.join();

            return;
        }

        // If already marked as stopped but join is still pending (e.g., self-stop path),
        // allow an external thread to finalize the join.
        // LCOV_EXCL_START
        // Reached only through a race, which is why no test drives it. A task that calls stop() from a
        // thread this executor owns cannot join the I/O thread, so it leaves the join to whoever comes
        // next - and this is that path. In every ordinary shutdown run() gets there first, because it
        // is woken by the same stop() that deferred the join. What is left is the window where an
        // outside stop() lands between the deferral and run() taking the thread, and a test that
        // sometimes covers it would be a test that sometimes does not.
        std::jthread thread_to_join;
        {
            const std::lock_guard thread_lock(io_thread_mutex_);
            if (io_thread_.joinable() && (io_thread_.get_id() != std::this_thread::get_id())) // LCOV_EXCL_BR_LINE
            {
                // request_stop() here and not only in stop(). stop() asks the I/O thread to finish only
                // on the call that wins running_.exchange(false), and run() publishes running_ = true
                // before it takes this mutex to create the thread. A stop landing in that gap finds no
                // thread to ask, and the run() that creates it a moment later sees running_ already
                // false and skips its own stop() - leaving a thread nobody asked to finish and a join
                // waiting on it for good. Asking again costs nothing when it was already requested.
                io_thread_.request_stop();
                thread_to_join = std::move(io_thread_);
            }
        }

        if (thread_to_join.joinable())
            thread_to_join.join();
        // LCOV_EXCL_STOP
    }

    std::expected<bool, std::error_code> executor::is_io_thread_affined_to(const int core_id) noexcept
    {
        if (core_id < 0)
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));

        cpu_set_t cpuset {};
        CPU_ZERO(&cpuset);

        int ret {};
        {
            const std::lock_guard thread_lock(io_thread_mutex_);
            if (!io_thread_.joinable())
                return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));

            ret = aio::detail::syscalls::pthread_getaffinity_np(io_thread_.native_handle(), sizeof(cpu_set_t), &cpuset);
        }

        if (ret != 0)
            return std::unexpected(std::error_code(ret, std::generic_category()));

        return CPU_ISSET(core_id, &cpuset) != 0;
    }

    bool executor::on_loop_thread() const noexcept
    {
        return t_current_loop_executor == this;
    }

    expected_size_t executor::submit() noexcept
    {
        // Left for the loop, which submits and waits in one call. Not when the submission queue is
        // running out of room, though: a batch of completions can resume a coroutine per entry, each
        // preparing one more SQE, and an SQE that finds no room is an operation that fails outright.
        // A quarter of the ring is kept as the margin that has to stay free.
        if (on_loop_thread() && (::io_uring_sq_space_left(&ring_) > (config_.ring_entries / 4u)))
            return std::size_t {};

        const int ret = detail::uring_syscalls::submit(&ring_);
        if (ret < 0)
        {
            metrics_.error_count.fetch_add(1u, mem_order);
            return std::unexpected(std::error_code(-ret, std::generic_category()));
        }

        return static_cast<std::size_t>(ret);
    }

    template <typename Prepare>
    task<std::expected<int, std::error_code>> executor::await_uring_result(Prepare&& prepare) noexcept(false)
    {
        io_context ctx {};
        auto* const sqe = ::io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
        {
            metrics_.submission_full_count.fetch_add(1u, mem_order);
            co_return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        }

        prepare(sqe, ctx);
        ::io_uring_sqe_set_data(sqe, &ctx);

        if (const auto sub = submit(); !sub)
            co_return std::unexpected(sub.error());

        metrics_.total_submissions.fetch_add(1u, mem_order);
        co_await io_awaiter {ctx};
        if (ctx.result < 0)
            co_return std::unexpected(std::error_code(-ctx.result, std::generic_category()));

        co_return ctx.result;
    }

    void executor::process_completions() noexcept
    {
        for (::io_uring_cqe* cqe {}; ::io_uring_peek_cqe(&ring_, &cqe) == 0;)
        {
            metrics_.total_completions.fetch_add(1u, mem_order);
            auto* const ctx = static_cast<io_context*>(::io_uring_cqe_get_data(cqe));
            if (ctx != nullptr)
            {
                ctx->result = cqe->res;
                // LCOV_EXCL_BR_LINE: the continuation is stored before the SQE is submitted, and both
                // happen inside await_suspend, so a reaped completion always has one.
                if (ctx->continuation) // LCOV_EXCL_BR_LINE
                    ctx->continuation.resume();
            }

            ::io_uring_cqe_seen(&ring_, cqe);
        }
    }

    void executor::event_loop(std::stop_token st) noexcept
    {
        pin_to_core();

        // Marks this thread as the executor's own for as long as the loop runs, so submit() can leave
        // its entries for the wait below to carry into the kernel.
        t_current_loop_executor = this;
        const struct loop_thread_marker
        {
            ~loop_thread_marker() noexcept { t_current_loop_executor = nullptr; }
        } marker {};

        // Initialize coroutine slab allocator for this event loop thread.
        // E.g. allocating 1024-byte frames for up to the maximum ring entries.
        // Adjust sizes according to actual task frame requirements.
        allocator::slab coro_allocator {1024u, std::max(1024u, config_.ring_entries * 4u)};
        set_thread_allocator(&coro_allocator);

        // Once stop() has been requested, spawned tasks may still be suspended
        // waiting on in-flight io_uring operations. Exiting the loop immediately
        // would abandon their coroutine frames without resuming them (leak) and
        // tear down the ring while operations referencing it are still pending.
        // Instead: ask the kernel to cancel every outstanding request (so
        // suspended coroutines resume with an error and unwind normally), then
        // keep draining completions until active_work_ reaches zero, bounded by
        // a timeout to avoid hanging shutdown forever on a stuck operation.
        bool cancel_issued = false;
        std::chrono::steady_clock::time_point drain_deadline {};

        while (!st.stop_requested() || (active_work_.load(mem_order) > 0u))
        {
            if (st.stop_requested())
            {
                if (!cancel_issued)
                {
                    if (auto* const cancel_sqe = ::io_uring_get_sqe(&ring_))
                    {
                        ::io_uring_prep_cancel(cancel_sqe, nullptr, IORING_ASYNC_CANCEL_ANY | IORING_ASYNC_CANCEL_ALL);
                        ::io_uring_sqe_set_data(cancel_sqe, nullptr);
                        if (const auto sub = submit(); !sub)
                            logger::log(logger::level::error, std::source_location::current(),
                                        "Failed to submit shutdown cancel-all request: {}", sub.error().message());
                    }

                    cancel_issued = true;
                    drain_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                }
                else if (std::chrono::steady_clock::now() >= drain_deadline)
                {
                    logger::log(logger::level::error, std::source_location::current(),
                                "Forced shutdown with {} task(s) still active after cancellation drain timeout",
                                active_work_.load(mem_order));
                    break;
                }
            }

            ::io_uring_cqe* cqe {};
            __kernel_timespec ts {};
            ts.tv_sec = {};
            ts.tv_nsec = 100'000'000; // 100ms timeout

            // Submits whatever the coroutines resumed on this thread have prepared and waits for the
            // next completion, in one io_uring_enter().
            const int ret = detail::uring_syscalls::submit_and_wait_timeout(&ring_, &cqe, 1u, &ts);

            // Reaped whatever the wait returned: a timeout, or an error such as a full completion
            // queue, does not mean there is nothing to collect, and peeking when there is not costs a
            // read of the ring head.
            process_completions();

            if ((ret < 0) && (ret != -ETIME) && (ret != -EINTR) && (ret != -EAGAIN) && (ret != -EBUSY))
            {
                metrics_.error_count.fetch_add(1u, mem_order);
                logger::log(logger::level::error, std::source_location::current(), "io_uring_submit_and_wait_timeout error: {}",
                            std::strerror(-ret));
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
        CPU_SET(static_cast<int>(config_.core_id), &cpuset);

        const int ret = aio::detail::syscalls::pthread_setaffinity_np(::pthread_self(), sizeof(cpu_set_t), &cpuset);
        if (ret != 0)
            logger::log(logger::level::warn, std::source_location::current(), "Failed to pin io_uring thread to core {}: {}",
                        config_.core_id, std::strerror(ret));
        else
            logger::log(logger::level::info, std::source_location::current(), "io_uring executor pinned to CPU core {}", config_.core_id);
    }

    executor::detached_task_wrapper executor::execute_task(task<void> tsk, executor* self, std::weak_ptr<void> lifetime) noexcept
    {
        try
        {
            co_await tsk;
        }
        catch (const std::exception& e)
        {
            logger::log(logger::level::error, std::source_location::current(), "Exception propagated to top-level completion task: {}",
                        e.what());
        }

        // The executor may already be gone if it was force-destroyed while this task was
        // still suspended (e.g. cancelled during the shutdown drain in event_loop()). Skip
        // touching its state in that case: nothing is waiting on it anymore.
        if (lifetime.expired())
            co_return;

        self->metrics_.total_tasks_completed.fetch_add(1u, mem_order);
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

    executor& executor::get_default() noexcept(false)
    {
        thread_local executor instance;
        return instance;
    }

} // namespace kmx::aio::completion
