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

        /// @brief The window in which the connections were actually running.
        struct run_window
        {
            std::atomic_size_t started {};  ///< Connections that have begun.
            std::atomic_size_t finished {}; ///< Connections that have ended.
            clock_t::time_point begin {};   ///< When the first one began.
            clock_t::time_point end {};     ///< When the last one ended.
        };

        /// @brief One connection's worth of traffic: write a byte, read it back, repeat.
        static task<void> echo_pair(completion::executor& exec, const fd_t write_fd, const fd_t read_fd, const std::size_t rounds,
                                    const std::size_t connections, std::atomic_size_t& completed, run_window& window) noexcept(false)
        {
            char out_byte {};
            char in_byte {};

            // Timed from the first connection starting to the last one finishing, rather than around
            // run(). The ring's set-up is not per-operation cost, and neither is the 100 ms the loop's
            // wait spends timing out once the work is done - which, spread over the operations, was most
            // of what this case used to report.
            if (window.started.fetch_add(1u, std::memory_order_relaxed) == 0u)
                window.begin = clock_t::now();

            for (std::size_t i {}; i != rounds; ++i)
            {
                if (!co_await exec.async_write(write_fd, cspan_char_t(&out_byte, 1u), 0u))
                    break;

                if (!co_await exec.async_read(read_fd, span_char_t(&in_byte, 1u), 0u))
                    break;

                completed.fetch_add(2u, std::memory_order_relaxed);
            }

            if ((window.finished.fetch_add(1u, std::memory_order_relaxed) + 1u) == connections)
                window.end = clock_t::now();
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
        out.note = "spawn() resumes the task inline on the calling thread, so this is one task start to finish";
        return out;
    }

    static result measure_completion_concurrent(std::string name, const std::size_t connections, const double scale)
    {
        // Many coroutines in flight at once, which is the shape a server actually has and the only one
        // in which submission batching can show: every operation prepared between two waits rides into
        // the kernel on the same io_uring_enter. The same total number of operations is run at every
        // width, so the per-operation figures compare directly and say whether the executor's own cost
        // grows with the number in flight - which no single width can tell anyone on its own.
        constexpr std::size_t total_rounds = 12'800u;
        const auto rounds = scaled(total_rounds / connections, scale);

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

            return skipped(std::move(name), "socketpair failed");
        }

        std::atomic_size_t completed {};
        completion_detail::run_window window {};

        {
            completion::executor exec {completion::executor_config {.ring_entries = 512u}};
            for (std::size_t i {}; i != connections; ++i)
                exec.spawn(completion_detail::echo_pair(exec, fds[i * 2u], fds[(i * 2u) + 1u], rounds, connections, completed, window));

            exec.run();
        }

        const auto elapsed = window.end - window.begin;
        for (const int fd: fds)
            ::close(fd);

        if (elapsed <= clock_t::duration::zero())
            return skipped(std::move(name), "no connection ran to completion");

        auto out = from_total(std::move(name), completed.load(std::memory_order_relaxed), elapsed);
        out.note = "one io_uring operation per figure, timed first start to last finish; compare with socketpair_rtt / 4";
        return out;
    }

    static result bench_completion_concurrent_1(const double scale)
    {
        return measure_completion_concurrent("completion/concurrent_echo (1 connection)", 1u, scale);
    }

    static result bench_completion_concurrent_8(const double scale)
    {
        return measure_completion_concurrent("completion/concurrent_echo (8 connections)", 8u, scale);
    }

    static result bench_completion_concurrent_64(const double scale)
    {
        return measure_completion_concurrent("completion/concurrent_echo (64 connections)", 64u, scale);
    }

    static result bench_completion_concurrent_256(const double scale)
    {
        return measure_completion_concurrent("completion/concurrent_echo (256 connections)", 256u, scale);
    }

    void register_completion_cases(registry& reg) noexcept(false)
    {
        reg.describe("completion", "the io_uring executor, to be read against the baseline round trips");

        reg.add("completion/spawn", bench_completion_spawn);
        reg.add("completion/rtt", bench_completion_rtt);
        reg.add("completion/concurrent_1", bench_completion_concurrent_1);
        reg.add("completion/concurrent_8", bench_completion_concurrent_8);
        reg.add("completion/concurrent_64", bench_completion_concurrent_64);
        reg.add("completion/concurrent_256", bench_completion_concurrent_256);
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
