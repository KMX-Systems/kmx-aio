/// @file aio/benchmark/feature/scenarios.hpp
/// @brief Benchmark scenarios written once and measured on both execution models.
/// @details A scenario in here is a template over a backend from backend_traits.hpp. Writing it once
///          is not only economy: two hand-written copies of "the same" benchmark drift, and the first
///          person to read the report has no way of telling a real difference between the executors
///          from a difference between the two benchmark bodies. One body cannot drift from itself.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <atomic>
    #include <chrono>
    #include <cstddef>
    #include <string>
    #include <thread>
    #include <utility>
    #include <vector>

    #include <kmx/aio/benchmark/feature/backend_traits.hpp>
    #include <kmx/aio/benchmark/harness.hpp>
#endif

namespace kmx::aio::benchmark::feature
{
    /// @brief Stops a run that has not finished within a deadline, so one hung case cannot stall the suite.
    /// @tparam StopFn A callable that asks the run to stop. It is called from the watchdog's thread.
    template <typename StopFn>
    class watchdog
    {
    public:
        /// @brief Starts watching.
        /// @param stop What to call when the deadline passes.
        /// @param limit How long to wait before calling it.
        /// @throws std::system_error if the thread cannot be started.
        watchdog(StopFn stop, const std::chrono::seconds limit) noexcept(false):
            thread_(
                [this, stop = std::move(stop), limit]() noexcept
                {
                    const auto deadline = std::chrono::steady_clock::now() + limit;
                    while (!done_.load(std::memory_order_acquire))
                    {
                        if (std::chrono::steady_clock::now() >= deadline)
                        {
                            expired_.store(true, std::memory_order_relaxed);
                            stop();
                            return;
                        }

                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                })
        {
        }

        /// @brief Stops watching and joins the thread.
        ~watchdog() noexcept { done_.store(true, std::memory_order_release); }

        watchdog(const watchdog&) = delete;
        watchdog& operator=(const watchdog&) = delete;

        /// @brief Whether the deadline passed.
        /// @return True when the run had to be stopped.
        [[nodiscard]] bool expired() const noexcept { return expired_.load(std::memory_order_relaxed); }

    private:
        /// @brief Set when the run finished on its own.
        std::atomic_bool done_ {};

        /// @brief Set when the deadline passed first.
        std::atomic_bool expired_ {};

        /// @brief The watching thread. Declared last, so it starts only once the flags exist.
        std::jthread thread_;
    };

    /// @brief Deduction guide, so a lambda can be handed straight to the constructor.
    template <typename StopFn>
    watchdog(StopFn, std::chrono::seconds) -> watchdog<StopFn>;

    /// @brief How long a scenario is given before the watchdog stops it.
    static constexpr std::chrono::seconds scenario_time_limit {60};

    /// @brief What each paired scenario is called and how much work it does.
    /// @details Held in one place so both sides of a pairing cannot disagree about it. A scenario
    ///          whose two sides ran different amounts of work is not a comparison, and the only
    ///          reliable way to stop that happening is for neither side to own the number.
    namespace catalogue
    {
        /// @brief The socketpair round-trip scenario.
        struct socketpair_rtt_scenario
        {
            /// @brief The key both sides register under.
            static constexpr std::string_view key = "socketpair_rtt";

            /// @brief What the row means, in one line.
            static constexpr std::string_view description =
                "one byte out and back between two coroutines, one round trip in flight at a time";

            /// @brief Round trips timed at scale 1.
            static constexpr std::size_t iterations = 20'000u;

            /// @brief Bytes carried per round trip.
            /// @details One. These cases measure the cost of getting the executor's attention, not the
            ///          cost of moving bytes; a larger payload measures the socket buffer as well and
            ///          blurs exactly the thing being compared. Throughput has its own scenarios.
            static constexpr std::size_t payload_size = 1u;
        };

        /// @brief The loopback TCP echo scenario, at one connection and at many.
        struct tcp_echo_scenario
        {
            /// @brief The key the single-connection pairing registers under.
            static constexpr std::string_view single_key = "tcp_echo_rtt (1 conn)";

            /// @brief What the single-connection row means.
            static constexpr std::string_view single_description =
                "64 bytes out and back over a loopback TCP connection, one round trip in flight";

            /// @brief The key the many-connection pairing registers under.
            static constexpr std::string_view many_key = "tcp_echo_rtt (64 conn)";

            /// @brief What the many-connection row means.
            static constexpr std::string_view many_description =
                "the same round trips spread over 64 connections, where submission batching can show";

            /// @brief Round trips per connection at scale 1, single-connection case.
            static constexpr std::size_t single_rounds = 5'000u;

            /// @brief Total round trips at scale 1, spread across the connections.
            static constexpr std::size_t many_total_rounds = 12'800u;

            /// @brief How many connections the many-connection case opens.
            static constexpr std::size_t connections = 64u;

            /// @brief Bytes per round trip. A payload that fits one segment and one read.
            static constexpr std::size_t payload_size = 64u;
        };

        /// @brief The loopback TCP bulk transfer scenario.
        /// @details A sweep over three block sizes rather than one, because the size is the variable
        ///          that decides this row. Each model has a fixed cost per I/O operation and the two
        ///          costs are not the same, so moving a fixed number of bytes in smaller pieces charges
        ///          that difference more times. A single figure would invite a conclusion about
        ///          "throughput" that is really a statement about one block size - and the TLS
        ///          throughput row, where the record pump works in 8 KiB chunks whatever the caller
        ///          asked for, is exactly the case that needs this sweep to be readable.
        struct tcp_throughput_scenario
        {
            static constexpr std::string_view small_key = "tcp_throughput (4 KiB)";   ///< The small-block pairing key.
            static constexpr std::string_view medium_key = "tcp_throughput (16 KiB)"; ///< The medium-block pairing key.
            static constexpr std::string_view large_key = "tcp_throughput (64 KiB)";  ///< The large-block pairing key.

            static constexpr std::string_view small_description = "4 KiB blocks streamed one way over loopback TCP";
            static constexpr std::string_view medium_description =
                "16 KiB blocks streamed one way over loopback TCP - the size the TLS pump works in";
            static constexpr std::string_view large_description = "64 KiB blocks streamed one way over loopback TCP";

            /// @brief Bytes moved at scale 1, held constant across the sweep.
            /// @details The same total at every size, so the sweep says what changing the block size
            ///          costs rather than what moving more bytes costs.
            static constexpr std::size_t total_bytes = 256u * 1024u * 1024u;

            static constexpr std::size_t small_block = 4'096u;   ///< Bytes per block, small.
            static constexpr std::size_t medium_block = 16'384u; ///< Bytes per block, medium.
            static constexpr std::size_t large_block = 65'536u;  ///< Bytes per block, large.
        };

        /// @brief The loopback TCP accept scenario.
        struct tcp_accept_scenario
        {
            static constexpr std::string_view key = "tcp_accept"; ///< The pairing key.
            static constexpr std::string_view description =
                "connect and accept, timed on the accepting side: IORING_OP_ACCEPT against epoll-then-accept";
            static constexpr std::size_t connections = 2'000u; ///< Connections accepted at scale 1.
        };

        /// @brief The loopback UDP round-trip scenario.
        struct udp_echo_scenario
        {
            static constexpr std::string_view key = "udp_echo_rtt"; ///< The pairing key.
            static constexpr std::string_view description = "a 64-byte datagram out and back between two loopback UDP endpoints";
            static constexpr std::size_t iterations = 10'000u; ///< Round trips at scale 1.
            static constexpr std::size_t payload_size = 64u;   ///< Bytes per datagram.
        };

        /// @brief The one-shot timer scenario.
        struct timer_scenario
        {
            static constexpr std::string_view key = "timer_oneshot (200 us)"; ///< The pairing key.
            static constexpr std::string_view description =
                "how late a 200 us timer actually fires: timerfd + epoll against IORING_OP_TIMEOUT";
            static constexpr std::size_t iterations = 2'000u;             ///< Timers awaited at scale 1.
            static constexpr std::chrono::nanoseconds interval {200'000}; ///< What each one asks for.
        };
    } // namespace catalogue

    namespace detail
    {
        /// @brief The far side of a ping-pong: read a payload, send it straight back.
        /// @tparam Backend The execution model to drive.
        /// @param exec The executor.
        /// @param fd The descriptor to echo on.
        /// @param iterations How many payloads to echo.
        /// @param payload_size The payload size in bytes.
        /// @throws std::bad_alloc (coroutine frame and buffer allocation).
        template <typename Backend>
        task<void> echo_side(typename Backend::executor_t& exec, const fd_t fd, const std::size_t iterations,
                             const std::size_t payload_size) noexcept(false)
        {
            std::vector<char> buffer(payload_size);
            for (std::size_t i {}; i != iterations; ++i)
            {
                if (!co_await Backend::read_exact(exec, fd, span_char_t(buffer.data(), buffer.size())))
                    co_return;

                if (!co_await Backend::write_exact(exec, fd, cspan_char_t(buffer.data(), buffer.size())))
                    co_return;
            }
        }

        /// @brief The near side of a ping-pong: send a payload, wait for it to come back, time it.
        /// @tparam Backend The execution model to drive.
        /// @param exec The executor.
        /// @param fd The descriptor to ping on.
        /// @param iterations How many round trips to make.
        /// @param payload_size The payload size in bytes.
        /// @param samples Receives one duration per round trip. Must outlive the run.
        /// @throws std::bad_alloc (coroutine frame and buffer allocation).
        template <typename Backend>
        task<void> ping_side(typename Backend::executor_t& exec, const fd_t fd, const std::size_t iterations,
                             const std::size_t payload_size, std::vector<double>& samples) noexcept(false)
        {
            std::vector<char> buffer(payload_size);
            for (std::size_t i {}; i != iterations; ++i)
            {
                const auto start = clock_t::now();

                if (!co_await Backend::write_exact(exec, fd, cspan_char_t(buffer.data(), buffer.size())))
                    co_return;

                if (!co_await Backend::read_exact(exec, fd, span_char_t(buffer.data(), buffer.size())))
                    co_return;

                samples.push_back(
                    static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));
            }
        }
    } // namespace detail

    /// @brief One payload out and back over a connected socket pair, timed per round trip.
    /// @details The floor of each executor: two coroutines, one round trip in flight at a time, so
    ///          there is nothing to batch and nothing to overlap. Both models run the identical two
    ///          coroutines over the identical socket pair; what differs is only how each waits.
    /// @tparam Backend The execution model to drive.
    /// @param name The case name.
    /// @param iterations How many round trips to time.
    /// @param payload_size The payload size in bytes.
    /// @return The measured result, or a skip when the machine would not give up a socket pair.
    /// @throws std::bad_alloc if the samples or the executor cannot be allocated.
    template <typename Backend>
    [[nodiscard]] result socketpair_rtt(std::string name, const std::size_t iterations, const std::size_t payload_size) noexcept(false)
    {
        int fd[2] {-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM | Backend::socket_flags, 0, fd) != 0)
            return skipped(std::move(name), "socketpair failed");

        auto hold = Backend::make();
        auto& exec = hold.get();

        if (!Backend::adopt(exec, fd[0]) || !Backend::adopt(exec, fd[1]))
        {
            ::close(fd[0]);
            ::close(fd[1]);
            return skipped(std::move(name), "the executor would not take the descriptors");
        }

        std::vector<double> samples {};
        samples.reserve(iterations);

        // The echo side first, so it is already waiting when the ping side sends its first payload.
        exec.spawn(detail::echo_side<Backend>(exec, fd[1], iterations, payload_size));
        exec.spawn(detail::ping_side<Backend>(exec, fd[0], iterations, payload_size, samples));

        {
            const watchdog guard {[&exec]() noexcept { exec.stop(); }, scenario_time_limit};
            exec.run();
        }

        ::close(fd[0]);
        ::close(fd[1]);

        if (samples.empty())
            return skipped(std::move(name), "no round trip completed");

        return from_samples(std::move(name), samples);
    }

    namespace detail
    {
        /// @brief The window in which the connections were actually running.
        /// @details Timing around run() would fold the executor's own start-up and its loop's final
        ///          wait timeout into the per-operation figure - on the completion side that wait is
        ///          100 ms, which spread over a few thousand operations is most of what the case would
        ///          report. A scenario that drives an executor times its own work instead: from the
        ///          first connection starting to the last one finishing.
        struct run_window
        {
            std::atomic_size_t started {};  ///< Connections that have begun.
            std::atomic_size_t finished {}; ///< Connections that have ended.
            clock_t::time_point begin {};   ///< When the first one began.
            clock_t::time_point end {};     ///< When the last one ended.

            /// @brief Marks a connection as started, stamping the window's opening on the first.
            void open() noexcept
            {
                if (started.fetch_add(1u, std::memory_order_relaxed) == 0u)
                    begin = clock_t::now();
            }

            /// @brief Marks a connection as finished, stamping the window's close on the last.
            /// @param total How many connections there are in all.
            void close(const std::size_t total) noexcept
            {
                if ((finished.fetch_add(1u, std::memory_order_relaxed) + 1u) == total)
                    end = clock_t::now();
            }
        };

        /// @brief Reads a whole buffer from a stream, however many reads that takes.
        /// @tparam Stream Either model's tcp::stream - both expose the same read().
        /// @param stream The stream to read.
        /// @param buffer The destination, filled completely.
        /// @return True on success, false when the peer went away or the read failed.
        /// @throws std::bad_alloc (coroutine frame allocation).
        template <typename Stream>
        task<bool> stream_read_exact(Stream& stream, const span_char_t buffer) noexcept(false)
        {
            std::size_t filled {};
            while (filled != buffer.size())
            {
                const auto n = co_await stream.read(span_char_t(buffer.data() + filled, buffer.size() - filled));
                if (!n || (*n == 0u))
                    co_return false;

                filled += *n;
            }

            co_return true;
        }

        /// @brief The server end of one TCP connection: read a payload, send it straight back.
        /// @tparam Backend The execution model to drive.
        /// @param exec The executor.
        /// @param fd The accepted connection.
        /// @param rounds How many payloads to echo.
        /// @param payload_size Bytes per payload.
        /// @throws std::bad_alloc (coroutine frame and buffer allocation).
        template <typename Backend>
        task<void> tcp_echo_server_side(typename Backend::executor_t& exec, file_descriptor fd, const std::size_t rounds,
                                        const std::size_t payload_size) noexcept(false)
        {
            typename Backend::tcp_stream_t stream {exec, std::move(fd)};
            std::vector<char> buffer(payload_size);

            for (std::size_t i {}; i != rounds; ++i)
            {
                if (!co_await stream_read_exact(stream, span_char_t(buffer.data(), buffer.size())))
                    co_return;

                if (!co_await stream.write_all(cspan_char_t(buffer.data(), buffer.size())))
                    co_return;
            }
        }

        /// @brief Accepts a fixed number of connections and gives each one an echo coroutine.
        /// @tparam Backend The execution model to drive.
        /// @param exec The executor.
        /// @param listener The listening socket. Must outlive the run.
        /// @param connections How many connections to accept.
        /// @param rounds How many payloads each connection echoes.
        /// @param payload_size Bytes per payload.
        /// @throws std::bad_alloc (coroutine frame allocation).
        template <typename Backend>
        task<void> tcp_acceptor(typename Backend::executor_t& exec, typename Backend::tcp_listener_t& listener,
                                const std::size_t connections, const std::size_t rounds, const std::size_t payload_size) noexcept(false)
        {
            for (std::size_t i {}; i != connections; ++i)
            {
                auto accepted = co_await listener.accept();
                if (!accepted)
                    co_return;

                exec.spawn(tcp_echo_server_side<Backend>(exec, std::move(*accepted), rounds, payload_size));
            }
        }

        /// @brief The client end of one TCP connection: send a payload, wait for it back, repeat.
        /// @tparam Backend The execution model to drive.
        /// @param exec The executor.
        /// @param port The loopback port to connect to.
        /// @param rounds How many round trips to make.
        /// @param payload_size Bytes per round trip.
        /// @param samples Receives one duration per round trip when not null. Must outlive the run.
        /// @param window The shared timing window. Must outlive the run.
        /// @param connections How many clients there are in all, so the last one can close the window.
        /// @param completed Counts the round trips that finished. Must outlive the run.
        /// @throws std::bad_alloc (coroutine frame and buffer allocation).
        template <typename Backend>
        task<void> tcp_echo_client_side(typename Backend::executor_t& exec, const port_t port, const std::size_t rounds,
                                        const std::size_t payload_size, std::vector<double>* const samples, run_window& window,
                                        const std::size_t connections, std::atomic_size_t& completed) noexcept(false)
        {
            auto connected = co_await Backend::connect(exec, port);
            if (!connected)
            {
                window.open();
                window.close(connections);
                co_return;
            }

            typename Backend::tcp_stream_t stream {exec, std::move(*connected)};
            std::vector<char> buffer(payload_size);

            // The connection is established before the window opens: a scenario measuring round trips
            // should not have the handshake averaged into them.
            window.open();

            for (std::size_t i {}; i != rounds; ++i)
            {
                const auto start = clock_t::now();

                if (!co_await stream.write_all(cspan_char_t(buffer.data(), buffer.size())))
                    break;

                if (!co_await stream_read_exact(stream, span_char_t(buffer.data(), buffer.size())))
                    break;

                if (samples != nullptr)
                    samples->push_back(
                        static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));

                completed.fetch_add(1u, std::memory_order_relaxed);
            }

            window.close(connections);
        }
    } // namespace detail

    /// @brief A payload out and back over loopback TCP, at a chosen number of connections.
    /// @details At one connection this is the latency floor: nothing to batch, nothing to overlap. At
    ///          many it is the shape a server actually has, and the only one in which io_uring's
    ///          submission batching can show - every operation prepared between two waits rides into
    ///          the kernel on the same io_uring_enter. The same total number of round trips runs at
    ///          every width, so the per-round-trip figures compare directly.
    /// @tparam Backend The execution model to drive.
    /// @param name The case name.
    /// @param connections How many connections to spread the work over.
    /// @param rounds_per_connection Round trips each connection makes.
    /// @param payload_size Bytes per round trip.
    /// @return The measured result, or a skip when the machine would not give up the sockets.
    /// @throws std::bad_alloc if the samples or the executor cannot be allocated.
    template <typename Backend>
    [[nodiscard]] result tcp_echo_rtt(std::string name, const std::size_t connections, const std::size_t rounds_per_connection,
                                      const std::size_t payload_size) noexcept(false)
    {
        auto hold = Backend::make();
        auto& exec = hold.get();

        // Port zero, then read back what the kernel chose: a fixed port would make this case fail
        // rather than measure whenever anything else on the machine happened to be using it.
        typename Backend::tcp_listener_t listener {exec, loopback(), 0u};
        if (!listener.listen(static_cast<int>(connections) + 64))
            return skipped(std::move(name), "listen failed");

        const auto port = bound_port(listener.get_fd());
        if (port == 0u)
            return skipped(std::move(name), "the listener reported no port");

        std::vector<double> samples {};
        std::atomic_size_t completed {};
        detail::run_window window {};

        // Per-round-trip samples only at one connection. With many in flight the samples interleave
        // across connections and a percentile over them describes no single connection's experience;
        // the throughput figure is the honest one there.
        const bool sampled = (connections == 1u);
        if (sampled)
            samples.reserve(rounds_per_connection);

        exec.spawn(detail::tcp_acceptor<Backend>(exec, listener, connections, rounds_per_connection, payload_size));
        for (std::size_t i {}; i != connections; ++i)
            exec.spawn(detail::tcp_echo_client_side<Backend>(exec, port, rounds_per_connection, payload_size, sampled ? &samples : nullptr,
                                                             window, connections, completed));

        {
            const watchdog guard {[&exec]() noexcept { exec.stop(); }, scenario_time_limit};
            exec.run();
        }

        const auto done = completed.load(std::memory_order_relaxed);
        if (done == 0u)
            return skipped(std::move(name), "no round trip completed");

        if (sampled)
            return from_samples(std::move(name), samples);

        const auto elapsed = window.end - window.begin;
        if (elapsed <= clock_t::duration::zero())
            return skipped(std::move(name), "no connection ran to completion");

        return from_total(std::move(name), done, elapsed);
    }

    /// @brief Blocks streamed one way over a loopback TCP connection.
    /// @details Bulk transfer rather than round trips: the sender never waits for the receiver, so the
    ///          figure is what it costs to get one block through the executor and into the socket, not
    ///          a latency. Read against tcp_echo_rtt it says how much of a round trip is the turn and
    ///          how much is the bytes.
    /// @tparam Backend The execution model to drive.
    /// @param name The case name.
    /// @param blocks How many blocks to send.
    /// @param block_size Bytes per block.
    /// @return The measured result, or a skip when the machine would not give up the sockets.
    /// @throws std::bad_alloc if the buffers or the executor cannot be allocated.
    template <typename Backend>
    [[nodiscard]] result tcp_throughput(std::string name, const std::size_t blocks, const std::size_t block_size) noexcept(false)
    {
        auto hold = Backend::make();
        auto& exec = hold.get();

        typename Backend::tcp_listener_t listener {exec, loopback(), 0u};
        if (!listener.listen(8))
            return skipped(std::move(name), "listen failed");

        const auto port = bound_port(listener.get_fd());
        if (port == 0u)
            return skipped(std::move(name), "the listener reported no port");

        std::atomic_size_t received_blocks {};
        detail::run_window window {};

        // A sink that reads everything the sender sends and counts whole blocks.
        const auto sink = [](typename Backend::executor_t& e, typename Backend::tcp_listener_t& l, const std::size_t count,
                             const std::size_t size, std::atomic_size_t& counter, detail::run_window& w) -> task<void>
        {
            auto accepted = co_await l.accept();
            if (!accepted)
                co_return;

            typename Backend::tcp_stream_t stream {e, std::move(*accepted)};
            std::vector<char> buffer(size);

            for (std::size_t i {}; i != count; ++i)
            {
                if (!co_await detail::stream_read_exact(stream, span_char_t(buffer.data(), buffer.size())))
                    break;

                counter.fetch_add(1u, std::memory_order_relaxed);
            }

            w.close(1u);
        };

        const auto source = [](typename Backend::executor_t& e, const port_t p, const std::size_t count, const std::size_t size,
                               detail::run_window& w) -> task<void>
        {
            auto connected = co_await Backend::connect(e, p);
            if (!connected)
                co_return;

            typename Backend::tcp_stream_t stream {e, std::move(*connected)};
            const std::vector<char> buffer(size);

            w.open();
            for (std::size_t i {}; i != count; ++i)
                if (!co_await stream.write_all(cspan_char_t(buffer.data(), buffer.size())))
                    break;
        };

        exec.spawn(sink(exec, listener, blocks, block_size, received_blocks, window));
        exec.spawn(source(exec, port, blocks, block_size, window));

        {
            const watchdog guard {[&exec]() noexcept { exec.stop(); }, scenario_time_limit};
            exec.run();
        }

        const auto done = received_blocks.load(std::memory_order_relaxed);
        const auto elapsed = window.end - window.begin;
        if ((done == 0u) || (elapsed <= clock_t::duration::zero()))
            return skipped(std::move(name), "no block arrived");

        return from_total(std::move(name), done, elapsed);
    }

    /// @brief Connections opened and accepted over loopback, as a throughput figure.
    /// @details The acceptor and the connector share one executor, so this is the rate at which the
    ///          pair gets a connection all the way up - IORING_OP_ACCEPT against the readiness model's
    ///          wait-then-accept, and on the client side one IORING_OP_CONNECT against a non-blocking
    ///          connect that has to report EINPROGRESS, wait for writability and read back SO_ERROR.
    ///          It is a rate rather than a latency: with both ends on one loop, the time an individual
    ///          accept appears to take is mostly the time the connector had not got round to it yet.
    /// @tparam Backend The execution model to drive.
    /// @param name The case name.
    /// @param connections How many connections to open.
    /// @return The measured result, or a skip when the machine would not give up the sockets.
    /// @throws std::bad_alloc if the executor cannot be allocated.
    template <typename Backend>
    [[nodiscard]] result tcp_accept(std::string name, const std::size_t connections) noexcept(false)
    {
        auto hold = Backend::make();
        auto& exec = hold.get();

        typename Backend::tcp_listener_t listener {exec, loopback(), 0u};
        if (!listener.listen(512))
            return skipped(std::move(name), "listen failed");

        const auto port = bound_port(listener.get_fd());
        if (port == 0u)
            return skipped(std::move(name), "the listener reported no port");

        std::atomic_size_t accepted_count {};
        detail::run_window window {};

        const auto acceptor = [](typename Backend::tcp_listener_t& l, const std::size_t count, std::atomic_size_t& counter,
                                 detail::run_window& w) -> task<void>
        {
            for (std::size_t i {}; i != count; ++i)
            {
                auto accepted = co_await l.accept();
                if (!accepted)
                    break;

                // Closed immediately: this case is about getting the connection up, and holding a
                // couple of thousand of them open would measure the descriptor table instead.
                counter.fetch_add(1u, std::memory_order_relaxed);
            }

            w.close(1u);
        };

        const auto connector = [](typename Backend::executor_t& e, const port_t p, const std::size_t count,
                                  detail::run_window& w) -> task<void>
        {
            w.open();
            for (std::size_t i {}; i != count; ++i)
            {
                auto connected = co_await Backend::connect(e, p);
                if (!connected)
                    break;
            }
        };

        exec.spawn(acceptor(listener, connections, accepted_count, window));
        exec.spawn(connector(exec, port, connections, window));

        {
            const watchdog guard {[&exec]() noexcept { exec.stop(); }, scenario_time_limit};
            exec.run();
        }

        const auto done = accepted_count.load(std::memory_order_relaxed);
        const auto elapsed = window.end - window.begin;
        if ((done == 0u) || (elapsed <= clock_t::duration::zero()))
            return skipped(std::move(name), "no connection was accepted");

        return from_total(std::move(name), done, elapsed);
    }

    /// @brief A datagram out and back between two loopback UDP endpoints, timed per round trip.
    /// @details The datagram path rather than the stream one: no connection, no ordering, and on the
    ///          completion side recvmsg and sendmsg rather than read and write. Both endpoints live on
    ///          one executor, so this is the same shape as the socketpair case with a different
    ///          transport under it.
    /// @tparam Backend The execution model to drive.
    /// @param name The case name.
    /// @param iterations How many round trips to time.
    /// @param payload_size Bytes per datagram.
    /// @return The measured result, or a skip when the machine would not give up the sockets.
    /// @throws std::bad_alloc if the samples or the executor cannot be allocated.
    template <typename Backend>
    [[nodiscard]] result udp_echo_rtt(std::string name, const std::size_t iterations, const std::size_t payload_size) noexcept(false)
    {
        auto hold = Backend::make();
        auto& exec = hold.get();

        auto client = Backend::udp_endpoint_t::create(exec, AF_INET);
        auto server = Backend::udp_endpoint_t::create(exec, AF_INET);
        if (!client || !server)
            return skipped(std::move(name), "the UDP endpoints could not be created");

        if (!bind_loopback(client->raw().get_fd(), 0u) || !bind_loopback(server->raw().get_fd(), 0u))
            return skipped(std::move(name), "bind failed");

        const auto client_port = bound_port(client->raw().get_fd());
        const auto server_port = bound_port(server->raw().get_fd());
        if ((client_port == 0u) || (server_port == 0u))
            return skipped(std::move(name), "an endpoint reported no port");

        std::vector<double> samples {};
        samples.reserve(iterations);

        const auto echo = [](typename Backend::udp_endpoint_t& endpoint, const std::size_t count, const std::size_t size) -> task<void>
        {
            std::vector<std::byte> buffer(size);
            ::sockaddr_storage peer {};
            ::socklen_t peer_length = sizeof(peer);

            for (std::size_t i {}; i != count; ++i)
            {
                peer_length = sizeof(peer);
                const auto received = co_await endpoint.recv(span_byte_t(buffer.data(), buffer.size()), peer, peer_length);
                if (!received)
                    co_return;

                if (!co_await endpoint.send(cspan_byte_t(buffer.data(), *received), reinterpret_cast<const ::sockaddr*>(&peer),
                                            peer_length))
                    co_return;
            }
        };

        const auto ping = [](typename Backend::udp_endpoint_t& endpoint, const port_t peer_port, const std::size_t count,
                             const std::size_t size, std::vector<double>& out) -> task<void>
        {
            std::vector<std::byte> buffer(size);
            ::sockaddr_storage peer {};
            ::socklen_t peer_length = sizeof(peer);

            for (std::size_t i {}; i != count; ++i)
            {
                const auto start = clock_t::now();

                if (!co_await endpoint.send(cspan_byte_t(buffer.data(), buffer.size()), loopback(), peer_port))
                    co_return;

                peer_length = sizeof(peer);
                if (!co_await endpoint.recv(span_byte_t(buffer.data(), buffer.size()), peer, peer_length))
                    co_return;

                out.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));
            }
        };

        exec.spawn(echo(*server, iterations, payload_size));
        exec.spawn(ping(*client, server_port, iterations, payload_size, samples));

        {
            const watchdog guard {[&exec]() noexcept { exec.stop(); }, scenario_time_limit};
            exec.run();
        }

        if (samples.empty())
            return skipped(std::move(name), "no round trip completed");

        return from_samples(std::move(name), samples);
    }

    /// @brief How much later than asked a one-shot timer actually fires.
    /// @details The figure is the *overshoot* - measured elapsed minus requested - not the elapsed
    ///          time, because the requested time is a constant both models pay identically and
    ///          including it would bury the difference under it. A 200 us request that comes back at
    ///          215 us reports 15 us.
    ///
    ///          This is the most asymmetric pairing in the suite: the readiness model arms a timerfd
    ///          and waits for it to become readable through epoll, while the completion model submits
    ///          an IORING_OP_TIMEOUT with no descriptor at all. The timer is created once and re-armed
    ///          per wait, which is what real code does - creating a timerfd per wait would measure
    ///          timerfd_create, and the completion model has nothing to compare that against.
    /// @tparam Backend The execution model to drive.
    /// @param name The case name.
    /// @param iterations How many waits to time.
    /// @param interval What each wait asks for.
    /// @return The measured result, or a skip when the machine would not give up a timer.
    /// @throws std::bad_alloc if the samples or the executor cannot be allocated.
    template <typename Backend>
    [[nodiscard]] result timer_oneshot(std::string name, const std::size_t iterations,
                                       const std::chrono::nanoseconds interval) noexcept(false)
    {
        auto hold = Backend::make();
        auto& exec = hold.get();

        auto handle = Backend::timer_handle::create(exec);
        if (!handle)
            return skipped(std::move(name), "the timer could not be created");

        std::vector<double> samples {};
        samples.reserve(iterations);

        const auto body = [](typename Backend::executor_t& e, typename Backend::timer_handle& t, const std::size_t count,
                             const std::chrono::nanoseconds wanted, std::vector<double>& out) -> task<void>
        {
            const auto wanted_ns = static_cast<double>(wanted.count());
            for (std::size_t i {}; i != count; ++i)
            {
                const auto start = clock_t::now();
                if (!co_await t.wait_for(e, wanted))
                    co_return;

                const auto elapsed =
                    static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count());

                // Clamped at zero: a timer that fires a hair early is the clock's granularity, not a
                // negative overshoot, and letting it through would drag the mean below what any wait cost.
                out.push_back((elapsed > wanted_ns) ? (elapsed - wanted_ns) : 0.0);
            }
        };

        exec.spawn(body(exec, *handle, iterations, interval, samples));

        {
            const watchdog guard {[&exec]() noexcept { exec.stop(); }, scenario_time_limit};
            exec.run();
        }

        if (samples.empty())
            return skipped(std::move(name), "no timer fired");

        return from_samples(std::move(name), samples);
    }

} // namespace kmx::aio::benchmark::feature
