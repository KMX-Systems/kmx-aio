/// @file aio/benchmark/readiness_cases.cpp
/// @brief Readiness-executor (epoll) benchmarks.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/benchmark/cases.hpp"

#if defined(KMX_AIO_FEATURE_READINESS)

    #include <atomic>
    #include <cerrno>
    #include <chrono>
    #include <memory>
    #include <string>
    #include <sys/socket.h>
    #include <thread>
    #include <unistd.h>
    #include <vector>

    #include <kmx/aio/readiness/descriptor/epoll.hpp>
    #include <kmx/aio/readiness/executor.hpp>

namespace kmx::aio::benchmark
{
    namespace readiness_detail
    {
        /// @brief Stops an executor that has not finished within a deadline, so a hang cannot stall a run.
        class watchdog
        {
        public:
            explicit watchdog(std::shared_ptr<readiness::executor> exec, const std::chrono::seconds limit) noexcept(false):
                thread_(
                    [this, exec = std::move(exec), limit]() noexcept
                    {
                        if (!done_.wait_until(false, std::chrono::steady_clock::now() + limit))
                        {
                            expired_.store(true, std::memory_order_relaxed);
                            exec->stop();
                        }
                    })
            {
            }

            ~watchdog() noexcept
            {
                done_.store(true, std::memory_order_release);
                done_.notify_all();
            }

            [[nodiscard]] bool expired() const noexcept { return expired_.load(std::memory_order_relaxed); }

        private:
            /// @brief Waits until the flag is set or the deadline passes.
            struct flag
            {
                std::atomic_bool value {};

                void store(const bool v, const std::memory_order order) noexcept { value.store(v, order); }
                void notify_all() noexcept { value.notify_all(); }

                [[nodiscard]] bool wait_until(const bool old, const std::chrono::steady_clock::time_point deadline) noexcept
                {
                    while (value.load(std::memory_order_acquire) == old)
                    {
                        if (std::chrono::steady_clock::now() >= deadline)
                            return false;

                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }

                    return true;
                }
            };

            flag done_ {};
            std::atomic_bool expired_ {};
            std::jthread thread_;
        };

        /// @brief Reads one byte, suspending on the executor whenever the descriptor is not ready.
        /// @return True on success, false when the wait was cancelled or the peer went away.
        static task<bool> read_byte(readiness::executor& exec, const fd_t fd) noexcept(false)
        {
            char byte {};
            while (true)
            {
                const auto n = ::read(fd, &byte, 1u);
                if (n == 1)
                    co_return true;

                if (n == 0)
                    co_return false;

                if (errno == EINTR)
                    continue;

                if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
                    co_return false;

                if (!co_await exec.wait_io(fd, readiness::event_type::read))
                    co_return false;
            }
        }

        static void write_byte(const fd_t fd) noexcept
        {
            const char byte {};
            const auto written = ::write(fd, &byte, 1u);
            keep(written);
        }

        static task<void> echo_side(readiness::executor& exec, const fd_t fd, const std::size_t iterations) noexcept(false)
        {
            for (std::size_t i {}; i != iterations; ++i)
            {
                if (!co_await read_byte(exec, fd))
                    co_return;

                write_byte(fd);
            }
        }

        static task<void> ping_side(readiness::executor& exec, const fd_t fd, const std::size_t iterations,
                                    std::vector<double>& samples) noexcept(false)
        {
            for (std::size_t i {}; i != iterations; ++i)
            {
                const auto start = clock_t::now();
                write_byte(fd);
                if (!co_await read_byte(exec, fd))
                    co_return;

                samples.push_back(
                    static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));
            }
        }

        static task<void> noop() noexcept(false)
        {
            co_return;
        }
    } // namespace readiness_detail

    static result measure_readiness_rtt(std::string name, const std::size_t iterations, const readiness::resumption_mode mode)
    {
        int fd[2] {-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fd) != 0)
            return skipped(std::move(name), "socketpair failed");

        const readiness::executor_config config {.thread_count = 1u, .max_events = 64u, .timeout_ms = 50u, .resumption = mode};
        auto exec = std::make_shared<readiness::executor>(config);
        if (!exec->register_fd(fd[0]) || !exec->register_fd(fd[1]))
        {
            ::close(fd[0]);
            ::close(fd[1]);
            return skipped(std::move(name), "register_fd failed");
        }

        std::vector<double> samples {};
        samples.reserve(iterations);

        exec->spawn(readiness_detail::echo_side(*exec, fd[1], iterations));
        exec->spawn(readiness_detail::ping_side(*exec, fd[0], iterations, samples));

        {
            const readiness_detail::watchdog guard {exec, std::chrono::seconds(60)};
            exec->run();
        }

        ::close(fd[0]);
        ::close(fd[1]);

        return from_samples(std::move(name), samples);
    }

    static result bench_readiness_rtt_scheduler(const double scale)
    {
        auto out =
            measure_readiness_rtt("readiness/socketpair_rtt (scheduler)", scaled(20'000u, scale), readiness::resumption_mode::scheduler);
        out.note = "same work as the epoll baseline, plus one scheduler hand-off per wake-up";
        return out;
    }

    static result bench_readiness_rtt_inline(const double scale)
    {
        auto out = measure_readiness_rtt("readiness/socketpair_rtt (inline)", scaled(20'000u, scale),
                                         readiness::resumption_mode::inline_on_io_thread);
        out.note = "resumed on the I/O thread that saw the event, so no hand-off";
        return out;
    }

    /// @brief Compares the two wait_events() overloads at the default event capacity.
    /// @details Both perform one epoll_wait that returns immediately with a single ready descriptor.
    ///          The difference is the buffer: the vector overload hands back a vector resized to the
    ///          number of events received, so the next call grows it again - and growing it
    ///          value-initializes what it adds.
    static result measure_wait_events(std::string name, const std::size_t iterations, const bool use_span)
    {
        int fd[2] {-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fd) != 0)
            return skipped(std::move(name), "socketpair failed");

        auto epoll_result = readiness::descriptor::epoll::create();
        if (!epoll_result)
        {
            ::close(fd[0]);
            ::close(fd[1]);
            return skipped(std::move(name), "epoll_create1 failed");
        }

        auto epoll_fd = std::move(*epoll_result);
        keep(epoll_fd.add_monitored_fd(fd[1], EPOLLIN).has_value());

        // Left readable for the whole run, so every wait returns one event without blocking.
        const char byte {};
        keep(::write(fd[0], &byte, 1u));

        constexpr int max_events = 1024;
        std::vector<epoll_event> buffer(max_events);

        const auto start = clock_t::now();
        for (std::size_t i {}; i != iterations; ++i)
        {
            if (use_span)
                keep(epoll_fd.wait_events(std::span(buffer), 0).value_or(0u));
            else
                keep(epoll_fd.wait_events(buffer, max_events, 0).has_value());
        }

        const auto elapsed = clock_t::now() - start;
        ::close(fd[0]);
        ::close(fd[1]);
        return from_total(std::move(name), iterations, elapsed);
    }

    static result bench_wait_events_vector(const double scale)
    {
        auto out = measure_wait_events("readiness/epoll wait_events (vector, 1024 slots)", scaled(500'000u, scale), false);
        out.note = "resizes the vector down to the event count, so the next wait zeroes the buffer again";
        return out;
    }

    static result bench_wait_events_span(const double scale)
    {
        auto out = measure_wait_events("readiness/epoll wait_events (span, 1024 slots)", scaled(500'000u, scale), true);
        out.note = "waits into a buffer the loop keeps; what the event loop now uses";
        return out;
    }

    static result bench_readiness_stop(const double scale)
    {
        // What a caller waits for between asking an idle executor to stop and getting its thread back.
        // The loop is parked in epoll_wait when the request arrives, so this measures how it learns of
        // one: either it is woken, or it finds out when the wait times out - config.timeout_ms, a fifth
        // of a second by default, spent waiting for a timer.
        const auto iterations = scaled(10u, scale);
        std::vector<double> samples {};
        samples.reserve(iterations);

        for (std::size_t i {}; i != iterations; ++i)
        {
            auto exec = std::make_shared<readiness::executor>(readiness::executor_config {});
            std::jthread runner {[exec]() { exec->run(); }};

            // Parked in the wait, rather than merely started.
            while (exec->get_stats().total_epoll_waits.load(std::memory_order_relaxed) == 0u)
                std::this_thread::sleep_for(std::chrono::microseconds(200));

            std::this_thread::sleep_for(std::chrono::milliseconds(2));

            const auto start = clock_t::now();
            exec->stop();
            samples.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));
        }

        auto out = from_samples("readiness/stop an idle executor", samples);
        out.note = "time from stop() to the event loop's thread being joined, at the default timeout_ms";
        return out;
    }

    static result bench_readiness_spawn(const double scale)
    {
        const auto iterations = scaled(100'000u, scale);
        auto exec = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 1u, .timeout_ms = 10u});

        const auto start = clock_t::now();
        for (std::size_t i {}; i != iterations; ++i)
            exec->spawn(readiness_detail::noop());

        exec->run();
        const auto elapsed = clock_t::now() - start;
        return from_total("readiness/spawn+complete noop task", iterations, elapsed);
    }

    void register_readiness_cases(registry& reg) noexcept(false)
    {
        reg.add("readiness/wait_events_vector", bench_wait_events_vector);
        reg.add("readiness/wait_events_span", bench_wait_events_span);
        reg.add("readiness/stop", bench_readiness_stop);
        reg.add("readiness/spawn", bench_readiness_spawn);
        reg.add("readiness/rtt_scheduler", bench_readiness_rtt_scheduler);
        reg.add("readiness/rtt_inline", bench_readiness_rtt_inline);
    }

} // namespace kmx::aio::benchmark

#else

namespace kmx::aio::benchmark
{
    void register_readiness_cases(registry&) noexcept(false)
    {
        // The readiness model is not part of this build.
    }
} // namespace kmx::aio::benchmark

#endif
