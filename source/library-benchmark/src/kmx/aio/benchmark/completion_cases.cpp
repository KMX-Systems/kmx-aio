/// @file aio/benchmark/completion_cases.cpp
/// @brief Completion-executor (io_uring) benchmarks.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/benchmark/cases.hpp>

#if defined(KMX_AIO_FEATURE_COMPLETION)

    #include <chrono>
    #include <cstddef>
    #include <sys/socket.h>
    #include <unistd.h>
    #include <vector>

    #include <kmx/aio/completion/executor.hpp>

namespace kmx::aio::benchmark
{
    namespace completion_detail
    {
        static task<void> rtt_body(completion::executor& exec, const fd_t client_fd, const fd_t server_fd, const std::size_t iterations,
                                   std::vector<double>& samples) noexcept(false)
        {
            char out_byte {};
            char in_byte {};

            for (std::size_t i {}; i != iterations; ++i)
            {
                const auto start = clock_t::now();

                if (!co_await exec.async_write(client_fd, cspan_char_t(&out_byte, 1u), 0u))
                    co_return;

                if (!co_await exec.async_read(server_fd, span_char_t(&in_byte, 1u), 0u))
                    co_return;

                if (!co_await exec.async_write(server_fd, cspan_char_t(&out_byte, 1u), 0u))
                    co_return;

                if (!co_await exec.async_read(client_fd, span_char_t(&in_byte, 1u), 0u))
                    co_return;

                samples.push_back(
                    static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));
            }
        }

        static task<void> noop() noexcept(false)
        {
            co_return;
        }

        /// @brief One connection's worth of traffic: write a byte, read it back, repeat.
        static task<void> echo_pair(completion::executor& exec, const fd_t write_fd, const fd_t read_fd, const std::size_t rounds,
                                    std::atomic_size_t& completed) noexcept(false)
        {
            char out_byte {};
            char in_byte {};

            for (std::size_t i {}; i != rounds; ++i)
            {
                if (!co_await exec.async_write(write_fd, cspan_char_t(&out_byte, 1u), 0u))
                    co_return;

                if (!co_await exec.async_read(read_fd, span_char_t(&in_byte, 1u), 0u))
                    co_return;

                completed.fetch_add(2u, std::memory_order_relaxed);
            }
        }
    } // namespace completion_detail

    static result bench_completion_rtt(const double scale)
    {
        const auto iterations = scaled(20'000u, scale);
        int fd[2] {-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0)
            return skipped("completion/socketpair_rtt", "socketpair failed");

        std::vector<double> samples {};
        samples.reserve(iterations);

        {
            completion::executor exec {completion::executor_config {.ring_entries = 256u}};
            exec.spawn(completion_detail::rtt_body(exec, fd[0], fd[1], iterations, samples));
            exec.run();
        }

        ::close(fd[0]);
        ::close(fd[1]);

        auto out = from_samples("completion/socketpair_rtt", samples);
        out.note = "one round trip = 4 io_uring operations, one at a time, so nothing to batch";
        return out;
    }

    static result bench_completion_spawn(const double scale)
    {
        const auto iterations = scaled(1'000'000u, scale);
        completion::executor exec {completion::executor_config {.ring_entries = 64u}};

        const auto start = clock_t::now();
        for (std::size_t i {}; i != iterations; ++i)
            exec.spawn(completion_detail::noop());

        const auto elapsed = clock_t::now() - start;
        auto out = from_total("completion/spawn+complete noop task", iterations, elapsed);
        out.note = "resumed inline on the calling thread, so no hand-off is involved";
        return out;
    }

    static result bench_completion_concurrent(const double scale)
    {
        // Many coroutines in flight at once, which is the shape a server actually has and the only one
        // in which submission batching can show: every operation prepared between two waits rides into
        // the kernel on the same io_uring_enter.
        constexpr std::size_t connections = 64u;
        const auto rounds = scaled(400u, scale);

        std::vector<int> fds {};
        fds.reserve(connections * 2u);
        for (std::size_t i {}; i != connections; ++i)
        {
            int pair[2] {-1, -1};
            if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0)
                break;

            fds.push_back(pair[0]);
            fds.push_back(pair[1]);
        }

        if (fds.size() != (connections * 2u))
        {
            for (const int fd: fds)
                ::close(fd);

            return skipped("completion/concurrent_echo (64 connections)", "socketpair failed");
        }

        std::atomic_size_t completed {};
        const auto start = clock_t::now();

        {
            completion::executor exec {completion::executor_config {.ring_entries = 512u}};
            for (std::size_t i {}; i != connections; ++i)
                exec.spawn(completion_detail::echo_pair(exec, fds[i * 2u], fds[(i * 2u) + 1u], rounds, completed));

            exec.run();
        }

        const auto elapsed = clock_t::now() - start;
        for (const int fd: fds)
            ::close(fd);

        auto out = from_total("completion/concurrent_echo (64 connections)", completed.load(std::memory_order_relaxed), elapsed);
        out.note = "one io_uring operation per figure, with 64 coroutines submitting concurrently";
        return out;
    }

    void register_completion_cases(registry& reg) noexcept(false)
    {
        reg.add("completion/spawn", bench_completion_spawn);
        reg.add("completion/rtt", bench_completion_rtt);
        reg.add("completion/concurrent", bench_completion_concurrent);
    }

} // namespace kmx::aio::benchmark

#else

namespace kmx::aio::benchmark
{
    void register_completion_cases(registry&) noexcept(false)
    {
        // The completion model is not part of this build.
    }
} // namespace kmx::aio::benchmark

#endif
