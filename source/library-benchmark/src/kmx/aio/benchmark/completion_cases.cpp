/// @file src/kmx/aio/benchmark/completion_cases.cpp
/// @brief Completion-executor (io_uring) benchmarks.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/benchmark/completion_cases.hpp>
#if defined(KMX_AIO_FEATURE_COMPLETION)
    #ifndef PCH
        #include <kmx/aio/benchmark/feature/catalogue.hpp>
        #include <kmx/aio/benchmark/feature/completion_backend.hpp>
        #include <kmx/aio/benchmark/feature/scenarios.hpp>
        #include <kmx/aio/completion/executor.hpp>

        #include <chrono>
        #include <cstddef>
        #include <vector>
        #include <sys/socket.h>
        #include <unistd.h>
    #endif

namespace kmx::aio::benchmark
{
    namespace completion_detail
    {
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

        /// @brief One connection's worth of traffic, and where its progress is recorded.
        struct echo_pair_params
        {
            completion::executor& exec;    ///< The executor both operations run on.
            fd_t write_fd {-1};            ///< The end the byte is written to.
            fd_t read_fd {-1};             ///< The end the byte is read back from.
            std::size_t rounds {};         ///< How many round trips to make.
            std::size_t connections {};    ///< How many connections there are in all, so the last one can close the window.
            std::atomic_size_t& completed; ///< Counts the operations that finished.
            run_window& window;            ///< The shared timing window.
        };

        /// @brief One connection's worth of traffic: write a byte, read it back, repeat.
        /// @param params The executor, the two ends, the work to do and where it is recorded.
        /// @throws std::bad_alloc (coroutine frame allocation).
        static task<void> echo_pair(const echo_pair_params params) noexcept(false)
        {
            char out_byte {};
            char in_byte {};
            auto& window = params.window;

            // Timed from the first connection starting to the last one finishing, rather than around
            // run(). The ring's set-up is not per-operation cost, and neither is the 100 ms the loop's
            // wait spends timing out once the work is done - which, spread over the operations, was most
            // of what this case used to report.
            if (window.started.fetch_add(1u, std::memory_order_relaxed) == 0u)
                window.begin = clock_t::now();

            for (std::size_t i {}; i != params.rounds; ++i)
            {
                if (!co_await params.exec.async_write(params.write_fd, cspan_char_t(&out_byte, 1u), 0u))
                    break;

                if (!co_await params.exec.async_read(params.read_fd, span_char_t(&in_byte, 1u), 0u))
                    break;

                params.completed.fetch_add(2u, std::memory_order_relaxed);
            }

            if ((window.finished.fetch_add(1u, std::memory_order_relaxed) + 1u) == params.connections)
                window.end = clock_t::now();
        }
    }

    /// @brief The io_uring side of the socketpair round trip.
    /// @details Runs the shared scenario body, which is what makes the pairing mean anything. This
    ///          case used to drive both ends of the socket from a single coroutine issuing four
    ///          operations in a row, while the readiness side ran two coroutines that had to hand off
    ///          to each other. Those are different amounts of work, and the difference was being
    ///          reported as a difference between the executors. Both sides now run the same two
    ///          coroutines; only the waiting differs.
    [[nodiscard]] static result bench_completion_rtt(const double scale)
    {
        using scenario = feature::catalogue::socketpair_rtt_scenario;

        return with_note(feature::socketpair_rtt<feature::completion_backend>("completion/socketpair_rtt",
                                                                              scaled(scenario::iterations, scale), scenario::payload_size),
                         "one round trip = 4 io_uring operations, one at a time, so nothing to batch");
    }

    [[nodiscard]] static result bench_completion_spawn(const double scale)
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

    namespace completion_detail
    {
        /// @brief Creates @p count connected socket pairs.
        /// @param count How many pairs to create.
        /// @return Both ends of every pair, or an empty list when any one of them could not be made.
        /// @note All or nothing: a partial set is closed rather than handed back, so a caller never has
        ///       to decide what to do with fewer connections than it asked for.
        [[nodiscard]] inline std::vector<int> make_socket_pairs(const std::size_t count)
        {
            std::vector<int> fds {};
            fds.reserve(count * 2u);
            for (std::size_t i {}; i != count; ++i)
            {
                int pair[2] {-1, -1};
                if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0)
                    break;

                fds.push_back(pair[0]);
                fds.push_back(pair[1]);
            }

            if (fds.size() == (count * 2u))
                return fds;

            for (const int fd: fds)
                ::close(fd);
            return {};
        }
    }

    [[nodiscard]] static result measure_completion_concurrent(std::string name, const std::size_t connections, const double scale)
    {
        // Many coroutines in flight at once, which is the shape a server actually has and the only one
        // in which submission batching can show: every operation prepared between two waits rides into
        // the kernel on the same io_uring_enter. The same total number of operations is run at every
        // width, so the per-operation figures compare directly and say whether the executor's own cost
        // grows with the number in flight - which no single width can tell anyone on its own.
        constexpr std::size_t total_rounds = 12'800u;
        const auto rounds = scaled(total_rounds / connections, scale);

        auto fds = completion_detail::make_socket_pairs(connections);
        if (fds.empty())
            return skipped(std::move(name), "socketpair failed");

        std::atomic_size_t completed {};
        completion_detail::run_window window {};

        {
            completion::executor exec {completion::executor_config {.ring_entries = 512u}};
            for (std::size_t i {}; i != connections; ++i)
                exec.spawn(completion_detail::echo_pair({.exec = exec,
                                                         .write_fd = fds[i * 2u],
                                                         .read_fd = fds[(i * 2u) + 1u],
                                                         .rounds = rounds,
                                                         .connections = connections,
                                                         .completed = completed,
                                                         .window = window}));

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

    [[nodiscard]] static result bench_completion_concurrent_1(const double scale)
    {
        return measure_completion_concurrent("completion/concurrent_echo (1 connection)", 1u, scale);
    }

    [[nodiscard]] static result bench_completion_concurrent_8(const double scale)
    {
        return measure_completion_concurrent("completion/concurrent_echo (8 connections)", 8u, scale);
    }

    [[nodiscard]] static result bench_completion_concurrent_64(const double scale)
    {
        return measure_completion_concurrent("completion/concurrent_echo (64 connections)", 64u, scale);
    }

    [[nodiscard]] static result bench_completion_concurrent_256(const double scale)
    {
        return measure_completion_concurrent("completion/concurrent_echo (256 connections)", 256u, scale);
    }

    [[nodiscard]] static result bench_completion_tcp_echo_1(const double scale)
    {
        using scenario = feature::catalogue::tcp_echo_scenario;

        return feature::tcp_echo_rtt<feature::completion_backend>("completion/tcp_echo_rtt (1 connection)", 1u,
                                                                  scaled(scenario::single_rounds, scale), scenario::payload_size);
    }

    [[nodiscard]] static result bench_completion_tcp_echo_many(const double scale)
    {
        using scenario = feature::catalogue::tcp_echo_scenario;

        return with_note(feature::tcp_echo_rtt<feature::completion_backend>(
                             "completion/tcp_echo_rtt (64 connections)", scenario::connections,
                             scaled(scenario::many_total_rounds / scenario::connections, scale), scenario::payload_size),
                         "round trips spread over 64 connections, timed first start to last finish");
    }

    [[nodiscard]] static result bench_completion_tcp_throughput_small(const double scale)
    {
        using scenario = feature::catalogue::tcp_throughput_scenario;

        return with_note(feature::tcp_throughput<feature::completion_backend>("completion/tcp_throughput (4 KiB blocks)",
                                                                              scaled(scenario::total_bytes / scenario::small_block, scale),
                                                                              scenario::small_block),
                         "streamed one way; the sender never waits, so this is the cost of getting one block through");
    }

    [[nodiscard]] static result bench_completion_tcp_throughput_medium(const double scale)
    {
        using scenario = feature::catalogue::tcp_throughput_scenario;

        return with_note(feature::tcp_throughput<feature::completion_backend>("completion/tcp_throughput (16 KiB blocks)",
                                                                              scaled(scenario::total_bytes / scenario::medium_block, scale),
                                                                              scenario::medium_block),
                         "streamed one way; the sender never waits, so this is the cost of getting one block through");
    }

    [[nodiscard]] static result bench_completion_tcp_throughput_large(const double scale)
    {
        using scenario = feature::catalogue::tcp_throughput_scenario;

        return with_note(feature::tcp_throughput<feature::completion_backend>("completion/tcp_throughput (64 KiB blocks)",
                                                                              scaled(scenario::total_bytes / scenario::large_block, scale),
                                                                              scenario::large_block),
                         "streamed one way; the sender never waits, so this is the cost of getting one block through");
    }

    [[nodiscard]] static result bench_completion_tcp_accept(const double scale)
    {
        using scenario = feature::catalogue::tcp_accept_scenario;

        return with_note(feature::tcp_accept<feature::completion_backend>("completion/tcp_accept", scaled(scenario::connections, scale)),
                         "connections brought all the way up, as a rate: both ends share one loop");
    }

    [[nodiscard]] static result bench_completion_udp_echo(const double scale)
    {
        using scenario = feature::catalogue::udp_echo_scenario;

        return feature::udp_echo_rtt<feature::completion_backend>("completion/udp_echo_rtt", scaled(scenario::iterations, scale),
                                                                  scenario::payload_size);
    }

    [[nodiscard]] static result bench_completion_timer(const double scale)
    {
        using scenario = feature::catalogue::timer_scenario;

        return with_note(feature::timer_oneshot<feature::completion_backend>("completion/timer_oneshot (200 us)",
                                                                             scaled(scenario::iterations, scale), scenario::interval),
                         "overshoot: how much later than the 200 us asked for the wait actually returned");
    }

    void register_completion_cases(registry& reg) noexcept(false)
    {
        reg.describe("completion", "the io_uring executor, to be read against the baseline round trips");

        reg.add("completion/spawn", bench_completion_spawn);
        reg.add_paired(feature::catalogue::socketpair_rtt_scenario::key, execution_model::completion, "completion/rtt",
                       bench_completion_rtt);
        reg.add_paired(feature::catalogue::tcp_echo_scenario::single_key, execution_model::completion, "completion/tcp_echo_1",
                       bench_completion_tcp_echo_1);
        reg.add_paired(feature::catalogue::tcp_echo_scenario::many_key, execution_model::completion, "completion/tcp_echo_64",
                       bench_completion_tcp_echo_many);
        reg.add_paired(feature::catalogue::tcp_throughput_scenario::small_key, execution_model::completion,
                       "completion/tcp_throughput_small", bench_completion_tcp_throughput_small);
        reg.add_paired(feature::catalogue::tcp_throughput_scenario::medium_key, execution_model::completion,
                       "completion/tcp_throughput_medium", bench_completion_tcp_throughput_medium);
        reg.add_paired(feature::catalogue::tcp_throughput_scenario::large_key, execution_model::completion,
                       "completion/tcp_throughput_large", bench_completion_tcp_throughput_large);
        reg.add_paired(feature::catalogue::tcp_accept_scenario::key, execution_model::completion, "completion/tcp_accept",
                       bench_completion_tcp_accept);
        reg.add_paired(feature::catalogue::udp_echo_scenario::key, execution_model::completion, "completion/udp_echo",
                       bench_completion_udp_echo);
        reg.add_paired(feature::catalogue::timer_scenario::key, execution_model::completion, "completion/timer_oneshot",
                       bench_completion_timer);
        reg.add("completion/concurrent_1", bench_completion_concurrent_1);
        reg.add("completion/concurrent_8", bench_completion_concurrent_8);
        reg.add("completion/concurrent_64", bench_completion_concurrent_64);
        reg.add("completion/concurrent_256", bench_completion_concurrent_256);
    }

}

#else

namespace kmx::aio::benchmark
{
    void register_completion_cases(registry&) noexcept(false)
    {
        // The completion model is not part of this build.
    }
}

#endif
