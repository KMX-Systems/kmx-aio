/// @file inc/kmx/aio/benchmark/feature/scenarios.hpp
/// @brief Benchmark scenarios written once and measured on both execution models.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details A scenario in here is a template over a backend from readiness_backend.hpp or completion_backend.hpp. Writing it once
///          is not only economy: two hand-written copies of "the same" benchmark drift, and the first
///          person to read the report has no way of telling a real difference between the executors
///          from a difference between the two benchmark bodies. One body cannot drift from itself.
#pragma once
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/benchmark/feature/backend_traits.hpp>
    #include <kmx/aio/benchmark/feature/detail/run_window.hpp>
    #include <kmx/aio/benchmark/feature/watchdog.hpp>
    #include <kmx/aio/benchmark/harness.hpp>
    #include <kmx/aio/file_descriptor.hpp>
    #include <kmx/aio/task.hpp>

    #include <atomic>
    #include <chrono>
    #include <cstddef>
    #include <string>
    #include <utility>
    #include <vector>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace kmx::aio::benchmark::feature
{
    /// @brief How long a scenario is given before the watchdog stops it.
    static constexpr std::chrono::seconds scenario_time_limit {60};

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

        /// @brief What the near side of a ping-pong drives, and where it records the round trips.
        /// @tparam Backend The execution model to drive.
        template <typename Backend>
        struct ping_side_params
        {
            typename Backend::executor_t& exec; ///< The executor.
            fd_t fd {-1};                       ///< The descriptor to ping on.
            std::size_t iterations {};          ///< How many round trips to make.
            std::size_t payload_size {};        ///< The payload size in bytes.
            std::vector<double>& samples;       ///< Receives one duration per round trip. Must outlive the run.
        };

        /// @brief The near side of a ping-pong: send a payload, wait for it to come back, time it.
        /// @tparam Backend The execution model to drive.
        /// @param params The executor, the descriptor, the work to do and where the samples go.
        /// @throws std::bad_alloc (coroutine frame and buffer allocation).
        template <typename Backend>
        task<void> ping_side(const ping_side_params<Backend> params) noexcept(false)
        {
            std::vector<char> buffer(params.payload_size);
            for (std::size_t i {}; i != params.iterations; ++i)
            {
                const auto start = clock_t::now();

                if (!co_await Backend::write_exact(params.exec, params.fd, cspan_char_t(buffer.data(), buffer.size())))
                    co_return;

                if (!co_await Backend::read_exact(params.exec, params.fd, span_char_t(buffer.data(), buffer.size())))
                    co_return;

                params.samples.push_back(
                    static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));
            }
        }
    }

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
        exec.spawn(detail::ping_side<Backend>(
            {.exec = exec, .fd = fd[0], .iterations = iterations, .payload_size = payload_size, .samples = samples}));

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

        /// @brief What the accepting side of the TCP echo scenario serves.
        /// @tparam Backend The execution model to drive.
        template <typename Backend>
        struct tcp_acceptor_params
        {
            typename Backend::executor_t& exec;         ///< The executor.
            typename Backend::tcp_listener_t& listener; ///< The listening socket. Must outlive the run.
            std::size_t connections {};                 ///< How many connections to accept.
            std::size_t rounds {};                      ///< How many payloads each connection echoes.
            std::size_t payload_size {};                ///< Bytes per payload.
        };

        /// @brief Accepts a fixed number of connections and gives each one an echo coroutine.
        /// @tparam Backend The execution model to drive.
        /// @param params The executor, the listener and the work each connection does.
        /// @throws std::bad_alloc (coroutine frame allocation).
        template <typename Backend>
        task<void> tcp_acceptor(const tcp_acceptor_params<Backend> params) noexcept(false)
        {
            for (std::size_t i {}; i != params.connections; ++i)
            {
                auto accepted = co_await params.listener.accept();
                if (!accepted)
                    co_return;

                params.exec.spawn(tcp_echo_server_side<Backend>(params.exec, std::move(*accepted), params.rounds, params.payload_size));
            }
        }

        /// @brief What the client end of one TCP echo connection does, and where it records it.
        /// @tparam Backend The execution model to drive.
        template <typename Backend>
        struct tcp_echo_client_side_params
        {
            typename Backend::executor_t& exec; ///< The executor.
            port_t port {};                     ///< The loopback port to connect to.
            std::size_t rounds {};              ///< How many round trips to make.
            std::size_t payload_size {};        ///< Bytes per round trip.
            std::vector<double>* samples {};    ///< Receives one duration per round trip when not null. Must outlive the run.
            run_window& window;                 ///< The shared timing window. Must outlive the run.
            std::size_t connections {};         ///< How many clients there are in all, so the last one can close the window.
            std::atomic_size_t& completed;      ///< Counts the round trips that finished. Must outlive the run.
        };

        /// @brief The client end of one TCP connection: send a payload, wait for it back, repeat.
        /// @tparam Backend The execution model to drive.
        /// @param params The executor, the port, the work to do and where it is recorded.
        /// @throws std::bad_alloc (coroutine frame and buffer allocation).
        template <typename Backend>
        task<void> tcp_echo_client_side(const tcp_echo_client_side_params<Backend> params) noexcept(false)
        {
            auto& window = params.window;
            auto connected = co_await Backend::connect(params.exec, params.port);
            if (!connected)
            {
                window.open();
                window.close(params.connections);
                co_return;
            }

            typename Backend::tcp_stream_t stream {params.exec, std::move(*connected)};
            std::vector<char> buffer(params.payload_size);

            // The connection is established before the window opens: a scenario measuring round trips
            // should not have the handshake averaged into them.
            window.open();

            for (std::size_t i {}; i != params.rounds; ++i)
            {
                const auto start = clock_t::now();

                if (!co_await stream.write_all(cspan_char_t(buffer.data(), buffer.size())))
                    break;

                if (!co_await stream_read_exact(stream, span_char_t(buffer.data(), buffer.size())))
                    break;

                if (params.samples != nullptr)
                    params.samples->push_back(
                        static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));

                params.completed.fetch_add(1u, std::memory_order_relaxed);
            }

            window.close(params.connections);
        }

        /// @brief What the receiving side of the TCP throughput scenario reads, and where it counts it.
        /// @tparam Backend The execution model to drive.
        template <typename Backend>
        struct block_sink_params
        {
            typename Backend::executor_t& exec;         ///< The executor the accepted stream is registered on.
            typename Backend::tcp_listener_t& listener; ///< The listener to accept the single connection from.
            std::size_t count {};                       ///< How many blocks to read.
            std::size_t size {};                        ///< Bytes per block.
            std::atomic_size_t& counter;                ///< Incremented once per whole block read.
            run_window& window;                         ///< Closed once the sink is done, stamping the measured window.
        };

        /// @brief Reads a fixed number of whole blocks off one accepted connection.
        /// @tparam Backend The execution model to drive.
        /// @param params The executor, the listener, the blocks to read and where they are counted.
        /// @throws std::bad_alloc (coroutine frame and buffer allocation).
        template <typename Backend>
        task<void> block_sink(const block_sink_params<Backend> params) noexcept(false)
        {
            auto accepted = co_await params.listener.accept();
            if (!accepted)
                co_return;

            typename Backend::tcp_stream_t stream {params.exec, std::move(*accepted)};
            std::vector<char> buffer(params.size);

            for (std::size_t i {}; i != params.count; ++i)
            {
                if (!co_await stream_read_exact(stream, span_char_t(buffer.data(), buffer.size())))
                    break;

                params.counter.fetch_add(1u, std::memory_order_relaxed);
            }

            params.window.close(1u);
        }

        /// @brief What the sending side of the TCP throughput scenario writes.
        /// @tparam Backend The execution model to drive.
        template <typename Backend>
        struct block_source_params
        {
            typename Backend::executor_t& exec; ///< The executor to connect on.
            port_t port {};                     ///< The loopback port to connect to.
            std::size_t count {};               ///< How many blocks to write.
            std::size_t size {};                ///< Bytes per block.
            run_window& window;                 ///< Opened once the connection is up, so the handshake stays out of the figure.
        };

        /// @brief Connects once and writes a fixed number of whole blocks.
        /// @tparam Backend The execution model to drive.
        /// @param params The executor, the port, the blocks to write and the window to open.
        /// @throws std::bad_alloc (coroutine frame and buffer allocation).
        template <typename Backend>
        task<void> block_source(const block_source_params<Backend> params) noexcept(false)
        {
            auto connected = co_await Backend::connect(params.exec, params.port);
            if (!connected)
                co_return;

            typename Backend::tcp_stream_t stream {params.exec, std::move(*connected)};
            const std::vector<char> buffer(params.size);

            params.window.open();
            for (std::size_t i {}; i != params.count; ++i)
                if (!co_await stream.write_all(cspan_char_t(buffer.data(), buffer.size())))
                    break;
        }

        /// @brief Accepts a fixed number of connections, closing each one as it arrives.
        /// @tparam Backend The execution model to drive.
        /// @param listener The listener to accept from.
        /// @param count How many connections to accept.
        /// @param counter Incremented once per accepted connection.
        /// @param window Closed once the acceptor is done, stamping the measured window.
        /// @throws std::bad_alloc (coroutine frame allocation).
        template <typename Backend>
        task<void> accept_counter(typename Backend::tcp_listener_t& listener, const std::size_t count, std::atomic_size_t& counter,
                                  run_window& window) noexcept(false)
        {
            for (std::size_t i {}; i != count; ++i)
            {
                auto accepted = co_await listener.accept();
                if (!accepted)
                    break;

                // Closed immediately: this case is about getting the connection up, and holding a
                // couple of thousand of them open would measure the descriptor table instead.
                counter.fetch_add(1u, std::memory_order_relaxed);
            }

            window.close(1u);
        }

        /// @brief Opens a fixed number of connections, one after another.
        /// @tparam Backend The execution model to drive.
        /// @param exec The executor to connect on.
        /// @param port The loopback port to connect to.
        /// @param count How many connections to open.
        /// @param window Opened before the first connect, since the connects are the measured work.
        /// @throws std::bad_alloc (coroutine frame allocation).
        template <typename Backend>
        task<void> connect_counter(typename Backend::executor_t& exec, const port_t port, const std::size_t count,
                                   run_window& window) noexcept(false)
        {
            window.open();
            for (std::size_t i {}; i != count; ++i)
            {
                auto connected = co_await Backend::connect(exec, port);
                if (!connected)
                    break;
            }
        }

        /// @brief Sends every datagram it receives straight back to where it came from.
        /// @tparam Backend The execution model to drive.
        /// @param endpoint The endpoint to receive on and reply from.
        /// @param count How many datagrams to echo.
        /// @param size Bytes per datagram.
        /// @throws std::bad_alloc (coroutine frame and buffer allocation).
        template <typename Backend>
        task<void> udp_echo_responder(typename Backend::udp_endpoint_t& endpoint, const std::size_t count,
                                      const std::size_t size) noexcept(false)
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
        }

        /// @brief What the pinging side of the UDP echo scenario sends, and where it records the round trips.
        /// @tparam Backend The execution model to drive.
        template <typename Backend>
        struct udp_echo_pinger_params
        {
            typename Backend::udp_endpoint_t& endpoint; ///< The endpoint to send from and receive on.
            port_t peer_port {};                        ///< The loopback port of the echoing endpoint.
            std::size_t count {};                       ///< How many round trips to time.
            std::size_t size {};                        ///< Bytes per datagram.
            std::vector<double>& samples;               ///< One nanosecond figure appended per completed round trip.
        };

        /// @brief Sends a datagram and waits for it to come back, timing each round trip.
        /// @tparam Backend The execution model to drive.
        /// @param params The endpoint, the peer's port, the datagrams to send and where the samples go.
        /// @throws std::bad_alloc (coroutine frame, buffer and sample allocation).
        template <typename Backend>
        task<void> udp_echo_pinger(const udp_echo_pinger_params<Backend> params) noexcept(false)
        {
            std::vector<std::byte> buffer(params.size);
            ::sockaddr_storage peer {};
            ::socklen_t peer_length = sizeof(peer);

            for (std::size_t i {}; i != params.count; ++i)
            {
                const auto start = clock_t::now();

                if (!co_await params.endpoint.send(cspan_byte_t(buffer.data(), buffer.size()), loopback(), params.peer_port))
                    co_return;

                peer_length = sizeof(peer);
                if (!co_await params.endpoint.recv(span_byte_t(buffer.data(), buffer.size()), peer, peer_length))
                    co_return;

                params.samples.push_back(
                    static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));
            }
        }

        /// @brief What the timer scenario waits for, and where it records how late each wait returned.
        /// @tparam Backend The execution model to drive.
        template <typename Backend>
        struct timer_overshoot_params
        {
            typename Backend::executor_t& exec;    ///< The executor the timer waits on.
            typename Backend::timer_handle& timer; ///< The timer, created once and re-armed per wait.
            std::size_t count {};                  ///< How many waits to time.
            std::chrono::nanoseconds wanted {};    ///< What each wait asks for.
            std::vector<double>& samples;          ///< One nanosecond overshoot figure appended per wait, clamped at zero.
        };

        /// @brief Re-arms one timer for a fixed number of waits, recording how late each one fires.
        /// @tparam Backend The execution model to drive.
        /// @param params The executor, the timer, the waits to time and where the samples go.
        /// @throws std::bad_alloc (coroutine frame and sample allocation).
        template <typename Backend>
        task<void> timer_overshoot(const timer_overshoot_params<Backend> params) noexcept(false)
        {
            const auto wanted = params.wanted;
            const auto wanted_ns = static_cast<double>(wanted.count());
            for (std::size_t i {}; i != params.count; ++i)
            {
                const auto start = clock_t::now();
                if (!co_await params.timer.wait_for(params.exec, wanted))
                    co_return;

                const auto elapsed =
                    static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count());

                // Clamped at zero: a timer that fires a hair early is the clock's granularity, not a
                // negative overshoot, and letting it through would drag the mean below what any wait cost.
                params.samples.push_back((elapsed > wanted_ns) ? (elapsed - wanted_ns) : 0.0);
            }
        }
    }

    /// @brief A finished round-trip run, as @ref rtt_result turns it into a result.
    struct rtt_result_params
    {
        std::string name {};              ///< The case's name.
        bool sampled {};                  ///< Whether per-round-trip samples were taken.
        std::vector<double>& samples;     ///< Those samples, when they were. Sorted in place.
        std::size_t done {};              ///< How many round trips completed.
        const detail::run_window& window; ///< When the first started and the last finished.
    };

    /// @brief Turns a finished run into a result.
    /// @param params The case's name, the samples when they were taken, the round trips completed and the timing window.
    /// @return The result, or a skip when nothing ran to completion.
    /// @throws std::bad_alloc if the name cannot be stored.
    [[nodiscard]] inline result rtt_result(rtt_result_params params) noexcept(false)
    {
        if (params.done == 0u)
            return skipped(std::move(params.name), "no round trip completed");
        if (params.sampled)
            return from_samples(std::move(params.name), params.samples);

        const auto elapsed = params.window.end - params.window.begin;
        if (elapsed <= clock_t::duration::zero())
            return skipped(std::move(params.name), "no connection ran to completion");
        return from_total(std::move(params.name), params.done, elapsed);
    }

    /// @brief Listens on an ephemeral loopback port and reports the port the kernel chose.
    /// @tparam Listener The backend's listener type.
    /// @param listener The listener to bring up.
    /// @param backlog The listen backlog.
    /// @return The bound port, or zero when the listener could not be brought up.
    /// @note Port zero, then read back what the kernel chose: a fixed port would make a case fail
    ///       rather than measure whenever anything else on the machine happened to be using it.
    template <typename Listener>
    [[nodiscard]] port_t listen_on_ephemeral_port(Listener& listener, const int backlog) noexcept(false)
    {
        if (!listener.listen(backlog))
            return 0u;
        return bound_port(listener.get_fd());
    }

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
        const auto port = listen_on_ephemeral_port(listener, static_cast<int>(connections) + 64);
        if (port == 0u)
            return skipped(std::move(name), "the listener could not be bound");

        std::vector<double> samples {};
        std::atomic_size_t completed {};
        detail::run_window window {};

        // Per-round-trip samples only at one connection. With many in flight the samples interleave
        // across connections and a percentile over them describes no single connection's experience;
        // the throughput figure is the honest one there.
        const bool sampled = (connections == 1u);
        if (sampled)
            samples.reserve(rounds_per_connection);

        exec.spawn(detail::tcp_acceptor<Backend>({.exec = exec,
                                                  .listener = listener,
                                                  .connections = connections,
                                                  .rounds = rounds_per_connection,
                                                  .payload_size = payload_size}));
        for (std::size_t i {}; i != connections; ++i)
            exec.spawn(detail::tcp_echo_client_side<Backend>({.exec = exec,
                                                              .port = port,
                                                              .rounds = rounds_per_connection,
                                                              .payload_size = payload_size,
                                                              .samples = sampled ? &samples : nullptr,
                                                              .window = window,
                                                              .connections = connections,
                                                              .completed = completed}));

        {
            const watchdog guard {[&exec]() noexcept { exec.stop(); }, scenario_time_limit};
            exec.run();
        }

        return rtt_result({.name = std::move(name),
                           .sampled = sampled,
                           .samples = samples,
                           .done = completed.load(std::memory_order_relaxed),
                           .window = window});
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

        // The sink reads everything the source sends and counts whole blocks.
        exec.spawn(detail::block_sink<Backend>(
            {.exec = exec, .listener = listener, .count = blocks, .size = block_size, .counter = received_blocks, .window = window}));
        exec.spawn(detail::block_source<Backend>({.exec = exec, .port = port, .count = blocks, .size = block_size, .window = window}));

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

        exec.spawn(detail::accept_counter<Backend>(listener, connections, accepted_count, window));
        exec.spawn(detail::connect_counter<Backend>(exec, port, connections, window));

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

        exec.spawn(detail::udp_echo_responder<Backend>(*server, iterations, payload_size));
        exec.spawn(detail::udp_echo_pinger<Backend>(
            {.endpoint = *client, .peer_port = server_port, .count = iterations, .size = payload_size, .samples = samples}));

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

        exec.spawn(detail::timer_overshoot<Backend>(
            {.exec = exec, .timer = *handle, .count = iterations, .wanted = interval, .samples = samples}));

        {
            const watchdog guard {[&exec]() noexcept { exec.stop(); }, scenario_time_limit};
            exec.run();
        }

        if (samples.empty())
            return skipped(std::move(name), "no timer fired");

        return from_samples(std::move(name), samples);
    }

}
