/// @file aio/readiness/executor.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/detail/syscalls.hpp>
#include "kmx/aio/readiness/executor.hpp"

#include "kmx/aio/error_code.hpp"
#include "kmx/aio/readiness/descriptor/timer.hpp"
#include "kmx/aio/readiness/openonload/extensions.hpp"
#include "kmx/logger.hpp"
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#include <span>
#include <string_view>
#include <vector>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

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

    /// @brief The executor whose event loop is running on this thread, if any.
    /// @details Set by process_events() on entry and cleared on exit, so a resumption can tell "I am
    ///          already on the core this executor owns" from "I am somewhere else" without reading
    ///          io_thread_, which shutdown moves out from under it.
    thread_local const executor* t_current_io_executor = nullptr;

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

        // LCOV_EXCL_BR_LINE: every enumerator has an arm above; the remaining edge is the one gcov
        // emits for a value no enumerator names.
        switch (config_.backend) // LCOV_EXCL_BR_LINE
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

        // The loop's way of being interrupted. A stop request otherwise changes nothing the loop is
        // waiting on, so it sits in epoll_wait until the timeout expires - and every shutdown pays for
        // it. Not fatal when it cannot be created: the loop then falls back to noticing the stop on its
        // next timeout, which is what it did before this descriptor existed.
        wake_fd_ = file_descriptor(::eventfd(0u, EFD_NONBLOCK | EFD_CLOEXEC));
        if (!wake_fd_.is_valid())
            logger::log(logger::level::warn, std::source_location::current(),
                        "eventfd creation failed ({}); shutdown will wait for the epoll timeout", std::strerror(errno));
        else if (const auto added = epoll_fd_.add_monitored_fd(wake_fd_.get(), EPOLLIN | EPOLLET); !added)
            logger::log(logger::level::warn, std::source_location::current(),
                        "the wake-up descriptor could not be registered ({}); shutdown will wait for the epoll timeout",
                        added.error().message());
    }

    void executor::wake_event_loop() const noexcept
    {
        if (!wake_fd_.is_valid())
            return;

        const std::uint64_t token = 1u;
        const auto written = ::write(wake_fd_.get(), &token, sizeof(token));
        // Nothing to do about a failure here: a full counter - the only way this fails on an eventfd -
        // means the loop has a wake-up pending already, which is what this was for.
        static_cast<void>(written);
    }

    void executor::drain_wake_events() const noexcept
    {
        std::uint64_t token {};
        while (::read(wake_fd_.get(), &token, sizeof(token)) == static_cast<ssize_t>(sizeof(token)))
        {
        }
    }

    executor::~executor() noexcept
    {
        stop();
    }

    expected_void_t executor::register_fd(const fd_t fd) noexcept
    {
        metrics_.total_registrations.fetch_add(1u, mem_order);

        // Re-arms the descriptor. The kernel hands out the lowest free number, so a descriptor cancelled
        // earlier is very likely to come back as an unrelated socket; leaving the old mark in place would
        // make every wait on the new one fail immediately.
        {
            const std::lock_guard lock(subscribers_mutex_);
            cancelled_fds_.erase(fd);
        }

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

        // Anything still waiting on this descriptor has to be resumed, not merely forgotten. Once the
        // descriptor leaves epoll no event can ever arrive for it, so a subscription dropped here would
        // leave its coroutine suspended for good: the frame is never destroyed, and the task holding it
        // never completes, so run() waits on work that can no longer make progress.
        //
        // Not remembered: the descriptor is leaving the executor, and a wait started after this point
        // would mean waiting on a descriptor its owner has already given up.
        cancel_waiters(fd, false);
    }

    void executor::cancel_io(const fd_t fd) noexcept
    {
        cancel_waiters(fd, true);
    }

    void executor::cancel_waiters(const fd_t fd, const bool remember) noexcept
    {
        std::vector<coroutine_handle_t> handles;

        {
            const std::lock_guard lock(subscribers_mutex_);

            // Marked before the existing waiters are collected, so that a subscription racing with this
            // cancellation is refused by subscribe() instead of being stored behind us.
            if (remember)
                cancelled_fds_.insert(fd);

            for (auto it = subscribers_.begin(); it != subscribers_.end();)
            {
                if (it->first.fd != fd)
                {
                    ++it;
                    continue;
                }

                for (const auto& waiting: it->second)
                {
                    // Set before the handle is resumed, and read by the coroutine only after it is:
                    // await_resume() then reports the wait as cancelled rather than as an event.
                    // LCOV_EXCL_BR_LINE: wait_io()'s awaiter always passes the address of its own
                    // member, so a stored subscription never carries a null here.
                    if (waiting.cancelled != nullptr) // LCOV_EXCL_BR_LINE
                        *waiting.cancelled = true;

                    handles.push_back(waiting.handle);
                }

                it = subscribers_.erase(it);
            }
        }

        // Resumed outside the lock: a resumed coroutine may subscribe again, or unregister another
        // descriptor, and either would deadlock against a lock still held here.
        for (const auto handle: handles)
        {
            // LCOV_EXCL_BR_LINE: a subscription is only ever stored with a live handle.
            if (handle) // LCOV_EXCL_BR_LINE
                resume_waiter(handle);
        }
    }

    bool executor::subscribe(const fd_t fd, const event_type type, coroutine_handle_t handle, bool* const cancelled) noexcept(false)
    {
        const std::lock_guard lock(subscribers_mutex_);

        // A cancel that arrived while the caller was deciding to wait must not be lost: refuse the
        // subscription here, under the same lock cancel_waiters() holds, rather than parking on a
        // descriptor that will never be woken.
        if (cancelled_fds_.contains(fd))
        {
            // LCOV_EXCL_BR_LINE: subscribe() is called only from wait_io()'s awaiter, which always
            // passes the address of its own member.
            if (cancelled != nullptr) // LCOV_EXCL_BR_LINE
                *cancelled = true;

            return false;
        }

        // operator[] might throw std::bad_alloc
        subscribers_[{fd, type}].push_back(waiter {handle, cancelled});
        return true;
    }

    void executor::spawn(task<void>&& t) noexcept(false)
    {
        active_work_.fetch_add(1u, mem_order);
        metrics_.total_tasks_spawned.fetch_add(1u, mem_order);
        auto self = shared_from_this();

        // Create and execute the detached task
        const auto dt = execute_task(std::move(t), std::move(self));

        // Through the same path as an I/O wake-up, so a task spawned from a coroutine already running
        // on the I/O thread of an inline executor starts there rather than being sent to a worker and
        // back. A spawn from anywhere else still goes to the scheduler, which is what every spawn does
        // in the default mode.
        resume_waiter(dt.handle);
    }

    task_returning_expected_size_t executor::async_recvmsg(const fd_t fd, ::msghdr* msg, const unsigned flags) noexcept(false)
    {
        if (fd < 0)
            co_return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
        if (msg == nullptr)
            co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));

        while (true)
        {
            const auto n = ::recvmsg(fd, msg, static_cast<int>(flags));
            if (n >= 0)
                co_return static_cast<std::size_t>(n);

            if (would_block(errno))
            {
                if (!co_await wait_io(fd, event_type::read))
                    co_return std::unexpected(to_std_error_code(error_code::operation_cancelled));
                continue;
            }

            co_return std::unexpected(error_from_errno());
        }
    }

    task_returning_expected_size_t executor::async_sendmsg(const fd_t fd, const ::msghdr* msg, const unsigned flags) noexcept(false)
    {
        if (fd < 0)
            co_return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
        if (msg == nullptr)
            co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));

        while (true)
        {
            const auto n = ::sendmsg(fd, msg, static_cast<int>(flags));
            if (n >= 0)
                co_return static_cast<std::size_t>(n);

            if (would_block(errno))
            {
                if (!co_await wait_io(fd, event_type::write))
                    co_return std::unexpected(to_std_error_code(error_code::operation_cancelled));
                continue;
            }

            co_return std::unexpected(error_from_errno());
        }
    }

    task_returning_expected_void_t executor::async_timeout(const std::uint64_t duration_ns) noexcept(false)
    {
        if (duration_ns == 0u)
            co_return expected_void_t {};

        auto timer_res = descriptor::timer::create();
        if (!timer_res)
            co_return std::unexpected(timer_res.error());

        auto timer_fd = std::move(*timer_res);
        if (const auto reg = register_fd(timer_fd.get()); !reg)
            co_return std::unexpected(reg.error());

        const struct unregister_guard
        {
            executor& exec;
            fd_t fd;
            ~unregister_guard() noexcept
            {
                // LCOV_EXCL_BR_LINE: the guard makes the type safe to construct with an invalid
                // descriptor; async_timeout only builds one around a timerfd it has already checked.
                if (fd >= 0) // LCOV_EXCL_BR_LINE
                    exec.unregister_fd(fd);
            }
        } guard {*this, timer_fd.get()};

        ::itimerspec spec {};
        spec.it_value.tv_sec = static_cast<decltype(::timespec::tv_sec)>(duration_ns / 1'000'000'000ULL);
        spec.it_value.tv_nsec = static_cast<decltype(::timespec::tv_nsec)>(duration_ns % 1'000'000'000ULL);
        if (const auto set = timer_fd.set_time(0, spec); !set)
            co_return std::unexpected(set.error());

        const auto wait = co_await timer_fd.wait(*this);
        if (!wait)
            co_return std::unexpected(wait.error());

        co_return expected_void_t {};
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
        {
            // Under idle_mutex_ for the same reason as in stop(): run()'s predicate reads active_work_,
            // and a notification that lands between its evaluation and the wait is lost.
            {
                const std::lock_guard idle_lock(self->idle_mutex_);
            }
            self->idle_cv_.notify_one();
        }
    }

    bool executor::on_owned_thread() const noexcept
    {
        // "Can this thread join the I/O thread?" is not the same question as "is this thread the I/O
        // thread?". A coroutine that calls stop() is resumed on a scheduler worker, and a worker that
        // joins the I/O thread lets run() return while the shutdown it is performing is still going -
        // after which the last reference to this executor can be dropped on the worker itself, and the
        // destructor tries to join the thread it is running on. Both kinds of owned thread have to
        // defer the join to a caller from outside.
        // LCOV_EXCL_START
        // The readiness backend resumes every coroutine on a scheduler worker - process_events only
        // schedules the resumption - so no library code calls stop() while running on the epoll thread
        // itself. The test is kept because it is the question actually being asked, and because a
        // future inline resumption would make it live.
        if (io_thread_.get_id() == std::this_thread::get_id())
            return true;
        // LCOV_EXCL_STOP

        // LCOV_EXCL_BR_LINE: scheduler_ is built in the constructor's init list and never reset, so
        // the null arm cannot be taken; it is here so the check reads as a complete question.
        return scheduler_ && scheduler_->is_worker_thread(); // LCOV_EXCL_BR_LINE
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

        // The wait is over; nothing below reads the idle state, and stop() now takes this mutex to
        // publish its change safely. Holding it across that call would be a thread locking a
        // non-recursive mutex it already owns.
        lock.unlock();

        if (running_.load(mem_order))
            stop();

        // If stop() was called from the I/O thread itself, join may be deferred.
        std::jthread thread_to_join;
        {
            const std::lock_guard thread_lock(io_thread_mutex_);
            if (io_thread_.joinable() && !on_owned_thread()) // LCOV_EXCL_BR_LINE
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

        // The I/O thread is done, but a scheduler worker may still be finishing the task that asked for
        // the shutdown, and that task can hold the last reference to this executor. Returning now would
        // let the caller drop its own reference first, leaving the destructor to run on a worker this
        // executor is about to join. Waiting here is what keeps the destruction on the caller's thread.
        // LCOV_EXCL_BR_LINE: as above - scheduler_ is never null.
        if (scheduler_) // LCOV_EXCL_BR_LINE
            scheduler_->wait_until_idle();
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
                const std::lock_guard lock(io_thread_mutex_);
                if (io_thread_.joinable()) // LCOV_EXCL_BR_LINE
                {
                    io_thread_.request_stop();

                    // The request alone changes nothing the loop is waiting on. This is what it is
                    // waiting on.
                    wake_event_loop();

                    // Avoid joining from a thread this executor owns. That happens whenever a task
                    // resumed by this executor calls stop(), whether it was resumed on the I/O thread
                    // or on a scheduler worker; the join is left to run(), or to a later stop() from
                    // outside.
                    if (on_owned_thread())
                        return;

                    thread_to_join = std::move(io_thread_);
                }
            }

            if (thread_to_join.joinable()) // LCOV_EXCL_BR_LINE
                thread_to_join.join();

            return;
        }

        // Allow external completion of deferred join.
        // LCOV_EXCL_START
        // Reached only through a race, which is why no test drives it. A task that calls stop() from a
        // thread this executor owns cannot join the I/O thread, so it leaves the join to whoever comes
        // next - and this is that path. In every ordinary shutdown run() gets there first, because it
        // is woken by the same stop() that deferred the join. What is left is the window where an
        // outside stop() lands between the deferral and run() taking the thread, and a test that
        // sometimes covers it would be a test that sometimes does not.
        std::jthread thread_to_join;
        {
            const std::lock_guard lock(io_thread_mutex_);
            if (io_thread_.joinable() && !on_owned_thread()) // LCOV_EXCL_BR_LINE
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
            const std::lock_guard lock(io_thread_mutex_);
            if (!io_thread_.joinable())
                return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));

            ret = aio::detail::syscalls::pthread_getaffinity_np(io_thread_.native_handle(), sizeof(cpu_set_t), &cpuset);
        }

        if (ret != 0)
            return std::unexpected(std::error_code(ret, std::generic_category()));

        return CPU_ISSET(core_id, &cpuset) != 0;
    }

    void executor::resume_if_found(const fd_t fd, const event_type type)
    {
        coroutine_handle_t handle {};
        {
            const std::lock_guard lock(subscribers_mutex_);
            const auto it = subscribers_.find({fd, type});
            if ((it != subscribers_.end()) && !it->second.empty())
            {
                handle = it->second.front().handle;
                it->second.pop_front();

                // The now-empty queue is left where it is. Erasing it here frees the map node and the
                // deque's block, and the very next wait on the same descriptor - which is what a socket
                // in a read loop does, on every single message - allocates both again. Descriptors are
                // handed back their number by the kernel too, so even a closed one tends to return to
                // the same entry. What does remove the entry is cancel_waiters(), which runs when the
                // descriptor is being taken away for good.
            }
        }

        if (handle)
            resume_waiter(handle);
    }

    bool executor::on_io_thread() const noexcept
    {
        return t_current_io_executor == this;
    }

    void executor::resume_waiter(const coroutine_handle_t handle) noexcept
    {
        // The resumed coroutine is part of a larger task whose lifetime is already tracked by the
        // wrapper in spawn(). What is not tracked is this executor: a resumed coroutine may drop the
        // last reference to it, so a strong reference is held across the resumption either way - the
        // scheduler closure carries one, and the inline path below holds one on the stack.
        auto self = shared_from_this();

        // Continuing here is the whole point of the inline mode: the event was observed on this thread,
        // the descriptor's data is in this core's cache, and handing the coroutine to a worker would
        // pay a wake-up and a context switch to move that work to a colder core. The scheduler path
        // stays for cancellations arriving from elsewhere, which are not this executor's I/O thread and
        // must not run application code on whatever thread called cancel_io().
        if ((config_.resumption == resumption_mode::inline_on_io_thread) && on_io_thread())
        {
            handle.resume();
            return;
        }

        scheduler_->spawn([self = std::move(self), handle]() { handle.resume(); });
    }

    void executor::process_events(std::stop_token st) noexcept(false)
    {
        static constexpr std::uint32_t read_mask = EPOLLIN | EPOLLERR | EPOLLHUP;
        static constexpr std::uint32_t write_mask = EPOLLOUT | EPOLLERR | EPOLLHUP;

        pin_to_core();

        // Marks this thread as the executor's own for as long as the loop runs, so resume_waiter() can
        // continue a coroutine here instead of handing it away.
        t_current_io_executor = this;
        const struct io_thread_marker
        {
            ~io_thread_marker() noexcept { t_current_io_executor = nullptr; }
        } marker {};

        // Allocated once and waited on over and over. The vector overload of wait_events() resizes to
        // the number of events it received, which means the next wait grows it back - and a vector
        // grown value-initializes what it adds, memsetting the whole buffer for values epoll_wait is
        // about to write. At the default max_events that is twelve kilobytes of zeroing per iteration.
        for (std::vector<epoll_event> events(config_.max_events);;)
        {
            metrics_.total_epoll_waits.fetch_add(1u, mem_order);
            const auto events_result = epoll_fd_.wait_events(std::span(events), config_.timeout_ms);
            if (!events_result)
            {
                if (events_result.error().value() == EINTR)
                    continue;
                metrics_.error_count.fetch_add(1u, mem_order);
                logger::log(logger::level::error, std::source_location::current(), "epoll_wait error: {}", events_result.error().message());
                break;
            }

            const auto ready = *events_result;
            if (ready != 0u)
            {
                metrics_.total_events_received.fetch_add(ready, mem_order);
                for (const auto& item: std::span(events).first(ready))
                {
                    const auto fd = item.data.fd;
                    const auto new_events = item.events;

                    // The loop's own wake-up, not a subscriber's descriptor. Drained so the next write
                    // to it produces a fresh edge.
                    if (fd == wake_fd_.get())
                    {
                        drain_wake_events();
                        continue;
                    }

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

        const int ret = aio::detail::syscalls::pthread_setaffinity_np(::pthread_self(), sizeof(cpu_set_t), &cpuset);
        if (ret != 0)
            logger::log(logger::level::warn, std::source_location::current(), "Failed to pin readiness thread to core {}: {}",
                        config_.core_id, std::strerror(ret));
        else
            logger::log(logger::level::info, std::source_location::current(), "Readiness executor pinned to CPU core {}", config_.core_id);
    }

} // namespace kmx::aio::readiness
