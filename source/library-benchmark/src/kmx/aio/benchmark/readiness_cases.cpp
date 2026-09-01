/// @file aio/benchmark/readiness_cases.cpp
/// @brief Readiness-executor (epoll) benchmarks.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/benchmark/cases.hpp>

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

    #include <kmx/aio/benchmark/feature/scenarios.hpp>
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

    /// @brief The epoll side of the socketpair round trip.
    /// @details Runs the shared scenario body rather than a copy of it, so the io_uring side is
    ///          measuring the identical two coroutines over the identical socket pair. It differs
    ///          from the case above only in resumption mode, which is what makes the pair fair: the
    ///          completion executor continues a coroutine on the thread that saw the completion, and
    ///          inline_on_io_thread is the readiness setting that does the same. Left at the default,
    ///          this side would additionally pay a scheduler hand-off per wake-up and the comparison
    ///          would report that hand-off as though it were the cost of epoll.
    static result bench_readiness_rtt_inline(const double scale)
    {
        using scenario = feature::catalogue::socketpair_rtt_scenario;

        return with_note(feature::socketpair_rtt<feature::readiness_backend>("readiness/socketpair_rtt (inline)",
                                                                             scaled(scenario::iterations, scale), scenario::payload_size),
                         "resumed on the I/O thread that saw the event, so no hand-off");
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
        // of a second by default, spent waiting for a timer. The join is inside the measurement because
        // stop() returning is not what a caller waits for; run() returning on the other thread is.
        const auto iterations = scaled(120u, scale);
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
            runner.join();
            samples.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));
        }

        auto out = from_samples("readiness/stop an idle executor", samples);
        out.note = "stop() to run() having returned on the executor's thread, at the default timeout_ms";
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

        auto out = from_total("readiness/spawn+drain noop tasks (queued, then run)", iterations, elapsed);
        out.note = "queued before run(): drain throughput with loop start-up in it, not comparable with the completion figure";
        return out;
    }

    static result bench_readiness_tcp_echo_1(const double scale)
    {
        using scenario = feature::catalogue::tcp_echo_scenario;

        return feature::tcp_echo_rtt<feature::readiness_backend>("readiness/tcp_echo_rtt (1 connection)", 1u,
                                                                 scaled(scenario::single_rounds, scale), scenario::payload_size);
    }

    static result bench_readiness_tcp_echo_many(const double scale)
    {
        using scenario = feature::catalogue::tcp_echo_scenario;

        return with_note(feature::tcp_echo_rtt<feature::readiness_backend>(
                             "readiness/tcp_echo_rtt (64 connections)", scenario::connections,
                             scaled(scenario::many_total_rounds / scenario::connections, scale), scenario::payload_size),
                         "round trips spread over 64 connections, timed first start to last finish");
    }

    static result bench_readiness_tcp_throughput_small(const double scale)
    {
        using scenario = feature::catalogue::tcp_throughput_scenario;

        return with_note(feature::tcp_throughput<feature::readiness_backend>("readiness/tcp_throughput (4 KiB blocks)",
                                                                             scaled(scenario::total_bytes / scenario::small_block, scale),
                                                                             scenario::small_block),
                         "streamed one way; the sender never waits, so this is the cost of getting one block through");
    }

    static result bench_readiness_tcp_throughput_medium(const double scale)
    {
        using scenario = feature::catalogue::tcp_throughput_scenario;

        return with_note(feature::tcp_throughput<feature::readiness_backend>("readiness/tcp_throughput (16 KiB blocks)",
                                                                             scaled(scenario::total_bytes / scenario::medium_block, scale),
                                                                             scenario::medium_block),
                         "streamed one way; the sender never waits, so this is the cost of getting one block through");
    }

    static result bench_readiness_tcp_throughput_large(const double scale)
    {
        using scenario = feature::catalogue::tcp_throughput_scenario;

        return with_note(feature::tcp_throughput<feature::readiness_backend>("readiness/tcp_throughput (64 KiB blocks)",
                                                                             scaled(scenario::total_bytes / scenario::large_block, scale),
                                                                             scenario::large_block),
                         "streamed one way; the sender never waits, so this is the cost of getting one block through");
    }

    static result bench_readiness_tcp_accept(const double scale)
    {
        using scenario = feature::catalogue::tcp_accept_scenario;

        return with_note(feature::tcp_accept<feature::readiness_backend>("readiness/tcp_accept", scaled(scenario::connections, scale)),
                         "connections brought all the way up, as a rate: both ends share one loop");
    }

    static result bench_readiness_udp_echo(const double scale)
    {
        using scenario = feature::catalogue::udp_echo_scenario;

        return feature::udp_echo_rtt<feature::readiness_backend>("readiness/udp_echo_rtt", scaled(scenario::iterations, scale),
                                                                 scenario::payload_size);
    }

    static result bench_readiness_timer(const double scale)
    {
        using scenario = feature::catalogue::timer_scenario;

        return with_note(feature::timer_oneshot<feature::readiness_backend>("readiness/timer_oneshot (200 us)",
                                                                            scaled(scenario::iterations, scale), scenario::interval),
                         "overshoot: how much later than the 200 us asked for the wait actually returned");
    }

    void register_readiness_cases(registry& reg) noexcept(false)
    {
        reg.describe("readiness", "the epoll executor, to be read against the baseline round trips");

        reg.add("readiness/wait_events_vector", bench_wait_events_vector);
        reg.add("readiness/wait_events_span", bench_wait_events_span);
        reg.add("readiness/stop", bench_readiness_stop);
        reg.add("readiness/spawn", bench_readiness_spawn);
        reg.add("readiness/rtt_scheduler", bench_readiness_rtt_scheduler);
        reg.add_paired(feature::catalogue::socketpair_rtt_scenario::key, execution_model::readiness, "readiness/rtt_inline",
                       bench_readiness_rtt_inline);
        reg.add_paired(feature::catalogue::tcp_echo_scenario::single_key, execution_model::readiness, "readiness/tcp_echo_1",
                       bench_readiness_tcp_echo_1);
        reg.add_paired(feature::catalogue::tcp_echo_scenario::many_key, execution_model::readiness, "readiness/tcp_echo_64",
                       bench_readiness_tcp_echo_many);
        reg.add_paired(feature::catalogue::tcp_throughput_scenario::small_key, execution_model::readiness, "readiness/tcp_throughput_small",
                       bench_readiness_tcp_throughput_small);
        reg.add_paired(feature::catalogue::tcp_throughput_scenario::medium_key, execution_model::readiness,
                       "readiness/tcp_throughput_medium", bench_readiness_tcp_throughput_medium);
        reg.add_paired(feature::catalogue::tcp_throughput_scenario::large_key, execution_model::readiness, "readiness/tcp_throughput_large",
                       bench_readiness_tcp_throughput_large);
        reg.add_paired(feature::catalogue::tcp_accept_scenario::key, execution_model::readiness, "readiness/tcp_accept",
                       bench_readiness_tcp_accept);
        reg.add_paired(feature::catalogue::udp_echo_scenario::key, execution_model::readiness, "readiness/udp_echo",
                       bench_readiness_udp_echo);
        reg.add_paired(feature::catalogue::timer_scenario::key, execution_model::readiness, "readiness/timer_oneshot",
                       bench_readiness_timer);
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
