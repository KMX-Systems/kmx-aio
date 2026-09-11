/// @file inc/kmx/aio/benchmark/feature/tls_scenarios.hpp
/// @brief TLS scenarios, written once and measured on both execution models.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details tls::stream is already a template over the stream underneath it, so the two models share
///          the whole TLS layer - the handshake, the record loops, the BIO pumping - and differ only
///          in the tcp::stream at the bottom. That makes these the cleanest pairings in the suite:
///          whatever the delta is, it is the transport, because there is nothing else it could be.
#pragma once
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/benchmark/feature/backend_traits.hpp>
    #include <kmx/aio/benchmark/feature/detail/run_window.hpp>
    #include <kmx/aio/benchmark/feature/detail/tls_contexts.hpp>
    #include <kmx/aio/benchmark/feature/scenarios.hpp>
    #include <kmx/aio/benchmark/feature/watchdog.hpp>
    #include <kmx/aio/benchmark/harness.hpp>
    #include <kmx/aio/task.hpp>
    #include <kmx/aio/tls/stream.hpp>

    #include <openssl/ssl.h>

    #include <atomic>
    #include <chrono>
    #include <cstddef>
    #include <memory>
    #include <string>
    #include <utility>
    #include <vector>
#endif

namespace kmx::aio::benchmark::feature
{
    /// @brief One TLS session, established over a fresh loopback TCP connection.
    /// @tparam Backend The execution model to drive.
    template <typename Backend>
    using tls_stream_t = kmx::aio::tls::stream<typename Backend::tcp_stream_t>;

    /// @brief Shared TLS streams kept alive for the length of a scenario.
    /// @tparam Backend The execution model to drive.
    template <typename Backend>
    using tls_stream_list_t = std::vector<std::shared_ptr<tls_stream_t<Backend>>>;

    namespace detail
    {
        /// @brief What the accepting side of the handshake scenario serves, and where it keeps the sessions.
        /// @tparam Backend The execution model to drive.
        template <typename Backend>
        struct tls_acceptor_params
        {
            typename Backend::executor_t& exec;         ///< The executor.
            typename Backend::tcp_listener_t& listener; ///< The listening socket. Must outlive the run.
            ::SSL_CTX* ctx {};                          ///< The server context. Must outlive the run.
            std::size_t count {};                       ///< How many sessions to accept.
            tls_stream_list_t<Backend>& out;            ///< Receives the established sessions, so they stay alive. Must outlive the run.
        };

        /// @brief The accepting side: take a connection, hand it a TLS session, handshake it.
        /// @tparam Backend The execution model to drive.
        /// @param params The executor, the listener, the context, the sessions to accept and where they are kept.
        /// @throws std::bad_alloc (coroutine frame allocation).
        template <typename Backend>
        task<void> tls_acceptor(const tls_acceptor_params<Backend> params) noexcept(false)
        {
            for (std::size_t i {}; i != params.count; ++i)
            {
                auto accepted = co_await params.listener.accept();
                if (!accepted)
                    co_return;

                auto session =
                    std::make_shared<tls_stream_t<Backend>>(typename Backend::tcp_stream_t {params.exec, std::move(*accepted)}, params.ctx);
                session->set_accept_state();

                // Kept alive by the caller's vector: the handshake below suspends, and a session
                // destroyed while its coroutine is parked on the socket takes the socket with it.
                params.out.push_back(session);
                params.exec.spawn([](std::shared_ptr<tls_stream_t<Backend>> s) -> task<void>
                                  { static_cast<void>(co_await s->handshake()); }(session));
            }
        }

        /// @brief What the connecting side of the handshake scenario does, and where it records the handshakes.
        /// @tparam Backend The execution model to drive.
        template <typename Backend>
        struct tls_handshake_client_params
        {
            typename Backend::executor_t& exec; ///< The executor.
            port_t port {};                     ///< The loopback port to connect to.
            ::SSL_CTX* ctx {};                  ///< The client context. Must outlive the run.
            std::size_t count {};               ///< How many handshakes to time.
            std::vector<double>& samples;       ///< One nanosecond figure appended per completed handshake.
        };

        /// @brief The connecting side of the handshake case: connect, handshake, time it, repeat.
        /// @tparam Backend The execution model to drive.
        /// @param params The executor, the port, the context, the handshakes to time and where the samples go.
        /// @throws std::bad_alloc (coroutine frame and sample allocation).
        template <typename Backend>
        task<void> tls_handshake_client(const tls_handshake_client_params<Backend> params) noexcept(false)
        {
            for (std::size_t i {}; i != params.count; ++i)
            {
                auto connected = co_await Backend::connect(params.exec, params.port);
                if (!connected)
                    co_return;

                tls_stream_t<Backend> session {typename Backend::tcp_stream_t {params.exec, std::move(*connected)}, params.ctx};
                session.set_connect_state();

                const auto start = clock_t::now();
                if (!co_await session.handshake())
                    co_return;

                params.samples.push_back(
                    static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));
            }
        }

        /// @brief What the accepting side of the TLS echo scenario serves, and where it keeps the session.
        /// @tparam Backend The execution model to drive.
        template <typename Backend>
        struct tls_echo_server_params
        {
            typename Backend::executor_t& exec;         ///< The executor.
            typename Backend::tcp_listener_t& listener; ///< The listening socket. Must outlive the run.
            ::SSL_CTX* ctx {};                          ///< The server context. Must outlive the run.
            std::size_t count {};                       ///< How many round trips to serve.
            std::size_t size {};                        ///< Bytes per round trip.
            tls_stream_list_t<Backend>& keep_alive;     ///< Holds the session, which outlives this coroutine's suspensions.
        };

        /// @brief The accepting side of the echo case: one session, then read a block and write it back.
        /// @tparam Backend The execution model to drive.
        /// @param params The executor, the listener, the context, the round trips to serve and where the session is kept.
        /// @throws std::bad_alloc (coroutine frame and buffer allocation).
        template <typename Backend>
        task<void> tls_echo_server(const tls_echo_server_params<Backend> params) noexcept(false)
        {
            auto accepted = co_await params.listener.accept();
            if (!accepted)
                co_return;

            auto session =
                std::make_shared<tls_stream_t<Backend>>(typename Backend::tcp_stream_t {params.exec, std::move(*accepted)}, params.ctx);
            params.keep_alive.push_back(session);
            session->set_accept_state();
            if (!co_await session->handshake())
                co_return;

            std::vector<char> buffer(params.size);
            for (std::size_t i {}; i != params.count; ++i)
            {
                if (!co_await stream_read_exact(*session, span_char_t(buffer.data(), buffer.size())))
                    co_return;

                if (!co_await session->write_all(cspan_char_t(buffer.data(), buffer.size())))
                    co_return;
            }
        }

        /// @brief What the connecting side of the TLS echo scenario does, and where it records it.
        /// @tparam Backend The execution model to drive.
        template <typename Backend>
        struct tls_echo_client_params
        {
            typename Backend::executor_t& exec; ///< The executor.
            port_t port {};                     ///< The loopback port to connect to.
            ::SSL_CTX* ctx {};                  ///< The client context. Must outlive the run.
            std::size_t count {};               ///< How many round trips to make.
            std::size_t size {};                ///< Bytes per round trip.
            std::vector<double>* samples {};    ///< One nanosecond figure appended per round trip, or nullptr to record none.
            std::atomic_size_t& completed;      ///< Incremented once per completed round trip.
            run_window& window;                 ///< Opened after the handshake and closed at the end, stamping the measured window.
        };

        /// @brief The connecting side of the echo case: handshake once, then time each round trip.
        /// @tparam Backend The execution model to drive.
        /// @param params The executor, the port, the context, the round trips to make and where they are recorded.
        /// @throws std::bad_alloc (coroutine frame, buffer and sample allocation).
        template <typename Backend>
        task<void> tls_echo_client(const tls_echo_client_params<Backend> params) noexcept(false)
        {
            auto connected = co_await Backend::connect(params.exec, params.port);
            if (!connected)
                co_return;

            tls_stream_t<Backend> session {typename Backend::tcp_stream_t {params.exec, std::move(*connected)}, params.ctx};
            session.set_connect_state();
            if (!co_await session.handshake())
                co_return;

            std::vector<char> buffer(params.size);

            // Opened after the handshake: this case is about the record layer, and averaging one
            // handshake over a few thousand round trips would quietly add it to every one of them.
            params.window.open();

            for (std::size_t i {}; i != params.count; ++i)
            {
                const auto start = clock_t::now();

                if (!co_await session.write_all(cspan_char_t(buffer.data(), buffer.size())))
                    break;

                if (!co_await stream_read_exact(session, span_char_t(buffer.data(), buffer.size())))
                    break;

                if (params.samples != nullptr)
                    params.samples->push_back(
                        static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));

                params.completed.fetch_add(1u, std::memory_order_relaxed);
            }

            params.window.close(1u);
        }

        /// @brief What the receiving side of the TLS throughput scenario reads, and where it counts it.
        /// @tparam Backend The execution model to drive.
        template <typename Backend>
        struct tls_block_sink_params
        {
            typename Backend::executor_t& exec;         ///< The executor.
            typename Backend::tcp_listener_t& listener; ///< The listening socket. Must outlive the run.
            ::SSL_CTX* ctx {};                          ///< The server context. Must outlive the run.
            std::size_t count {};                       ///< How many blocks to read.
            std::size_t size {};                        ///< Bytes per block.
            std::atomic_size_t& counter;                ///< Incremented once per whole block read.
            run_window& window;                         ///< Closed once the sink is done, stamping the measured window.
            tls_stream_list_t<Backend>& keep_alive;     ///< Holds the session, which outlives this coroutine's suspensions.
        };

        /// @brief The receiving side of the throughput case: one session, then count whole blocks.
        /// @tparam Backend The execution model to drive.
        /// @param params The executor, the listener, the context, the blocks to read and where they are counted.
        /// @throws std::bad_alloc (coroutine frame and buffer allocation).
        template <typename Backend>
        task<void> tls_block_sink(const tls_block_sink_params<Backend> params) noexcept(false)
        {
            auto accepted = co_await params.listener.accept();
            if (!accepted)
                co_return;

            auto session =
                std::make_shared<tls_stream_t<Backend>>(typename Backend::tcp_stream_t {params.exec, std::move(*accepted)}, params.ctx);
            params.keep_alive.push_back(session);
            session->set_accept_state();
            if (!co_await session->handshake())
            {
                params.window.close(1u);
                co_return;
            }

            std::vector<char> buffer(params.size);
            for (std::size_t i {}; i != params.count; ++i)
            {
                if (!co_await stream_read_exact(*session, span_char_t(buffer.data(), buffer.size())))
                    break;

                params.counter.fetch_add(1u, std::memory_order_relaxed);
            }

            params.window.close(1u);
        }

        /// @brief What the sending side of the TLS throughput scenario writes, and where it keeps the session.
        /// @tparam Backend The execution model to drive.
        template <typename Backend>
        struct tls_block_source_params
        {
            typename Backend::executor_t& exec;     ///< The executor.
            port_t port {};                         ///< The loopback port to connect to.
            ::SSL_CTX* ctx {};                      ///< The client context. Must outlive the run.
            std::size_t count {};                   ///< How many blocks to write.
            std::size_t size {};                    ///< Bytes per block.
            run_window& window;                     ///< Opened after the handshake, so the asymmetric crypto is not spread over the blocks.
            tls_stream_list_t<Backend>& keep_alive; ///< Holds the session, which must outlive the blocks still in flight.
        };

        /// @brief The sending side of the throughput case: handshake once, then stream blocks one way.
        /// @tparam Backend The execution model to drive.
        /// @param params The executor, the port, the context, the blocks to write and where the session is kept.
        /// @throws std::bad_alloc (coroutine frame and buffer allocation).
        template <typename Backend>
        task<void> tls_block_source(const tls_block_source_params<Backend> params) noexcept(false)
        {
            auto connected = co_await Backend::connect(params.exec, params.port);
            if (!connected)
                co_return;

            auto session =
                std::make_shared<tls_stream_t<Backend>>(typename Backend::tcp_stream_t {params.exec, std::move(*connected)}, params.ctx);
            params.keep_alive.push_back(session);
            session->set_connect_state();
            if (!co_await session->handshake())
                co_return;

            const std::vector<char> buffer(params.size);

            params.window.open();
            for (std::size_t i {}; i != params.count; ++i)
                if (!co_await session->write_all(cspan_char_t(buffer.data(), buffer.size())))
                    break;
        }
    }

    /// @brief A full TLS 1.3 handshake over a fresh loopback TCP connection, timed per handshake.
    /// @details The clock starts once the TCP connection is up, so the figure is the handshake and not
    ///          the connect that had to precede it - `tcp_accept` measures that separately. One session
    ///          at a time: a handshake is mostly asymmetric crypto and two in flight would measure the
    ///          core's throughput at RSA rather than the executor's at driving the record pump.
    /// @tparam Backend The execution model to drive.
    /// @param name The case name.
    /// @param iterations How many handshakes to time.
    /// @return The measured result, or a skip when the machine has no usable certificate.
    /// @throws std::bad_alloc if the samples or the executor cannot be allocated.
    template <typename Backend>
    [[nodiscard]] result tls_handshake(std::string name, const std::size_t iterations) noexcept(false)
    {
        detail::tls_contexts contexts {};
        if (!contexts.configure())
            return skipped(std::move(name), "openssl(1) produced no usable certificate");

        auto hold = Backend::make();
        auto& exec = hold.get();

        typename Backend::tcp_listener_t listener {exec, loopback(), 0u};
        if (!listener.listen(64))
            return skipped(std::move(name), "listen failed");

        const auto port = bound_port(listener.get_fd());
        if (port == 0u)
            return skipped(std::move(name), "the listener reported no port");

        std::vector<double> samples {};
        samples.reserve(iterations);
        tls_stream_list_t<Backend> server_sessions {};
        server_sessions.reserve(iterations);

        exec.spawn(detail::tls_acceptor<Backend>(
            {.exec = exec, .listener = listener, .ctx = contexts.server.get(), .count = iterations, .out = server_sessions}));
        exec.spawn(detail::tls_handshake_client<Backend>(
            {.exec = exec, .port = port, .ctx = contexts.client.get(), .count = iterations, .samples = samples}));

        {
            const watchdog guard {[&exec]() noexcept { exec.stop(); }, scenario_time_limit};
            exec.run();
        }

        if (samples.empty())
            return skipped(std::move(name), "no handshake completed");

        return from_samples(std::move(name), samples);
    }

    /// @brief Traffic through one established TLS session.
    /// @details The handshake happens once, outside the timed window, so this is the record layer and
    ///          the transport under it - encrypt, write, read, decrypt - with none of the asymmetric
    ///          crypto that dominates a handshake. Read against the plain `tcp_echo_rtt` it says what
    ///          the TLS layer adds; read against the other model's figure it says what the transport
    ///          contributes once TLS is in the path.
    /// @tparam Backend The execution model to drive.
    /// @param name The case name.
    /// @param rounds How many round trips to make.
    /// @param payload_size Bytes per round trip.
    /// @return The measured result, or a skip when the session could not be established.
    /// @throws std::bad_alloc if the buffers or the executor cannot be allocated.
    template <typename Backend>
    [[nodiscard]] result tls_echo_rtt(std::string name, const std::size_t rounds, const std::size_t payload_size) noexcept(false)
    {
        detail::tls_contexts contexts {};
        if (!contexts.configure())
            return skipped(std::move(name), "openssl(1) produced no usable certificate");

        auto hold = Backend::make();
        auto& exec = hold.get();

        typename Backend::tcp_listener_t listener {exec, loopback(), 0u};
        const auto port = listen_on_ephemeral_port(listener, 8);
        if (port == 0u)
            return skipped(std::move(name), "the listener could not be bound");

        std::vector<double> samples {};
        samples.reserve(rounds);

        std::atomic_size_t completed {};
        detail::run_window window {};
        tls_stream_list_t<Backend> server_sessions {};

        exec.spawn(detail::tls_echo_server<Backend>({.exec = exec,
                                                     .listener = listener,
                                                     .ctx = contexts.server.get(),
                                                     .count = rounds,
                                                     .size = payload_size,
                                                     .keep_alive = server_sessions}));
        exec.spawn(detail::tls_echo_client<Backend>({.exec = exec,
                                                     .port = port,
                                                     .ctx = contexts.client.get(),
                                                     .count = rounds,
                                                     .size = payload_size,
                                                     .samples = &samples,
                                                     .completed = completed,
                                                     .window = window}));

        {
            const watchdog guard {[&exec]() noexcept { exec.stop(); }, scenario_time_limit};
            exec.run();
        }

        if (completed.load(std::memory_order_relaxed) == 0u)
            return skipped(std::move(name), "no round trip completed");

        return from_samples(std::move(name), samples);
    }

    /// @brief Blocks streamed one way through an established TLS session.
    /// @details One-way, like the plain `tcp_throughput` it is meant to be read against, and for a
    ///          reason worth recording: written as a round trip instead, this case reported 80 ms per
    ///          16 KiB block on both models. That is Nagle meeting the peer's delayed ACK - the
    ///          library sets TCP_NODELAY on Modbus sockets and nowhere else, so a strict request and
    ///          response of a block that ends in a partial segment waits out the ACK timer twice. A
    ///          throughput case that never turns the connection around does not meet it, and neither
    ///          number would have said anything about the executors.
    /// @tparam Backend The execution model to drive.
    /// @param name The case name.
    /// @param blocks How many blocks to send.
    /// @param block_size Bytes per block.
    /// @return The measured result, or a skip when the session could not be established.
    /// @throws std::bad_alloc if the buffers or the executor cannot be allocated.
    template <typename Backend>
    [[nodiscard]] result tls_throughput(std::string name, const std::size_t blocks, const std::size_t block_size) noexcept(false)
    {
        detail::tls_contexts contexts {};
        if (!contexts.configure())
            return skipped(std::move(name), "openssl(1) produced no usable certificate");

        auto hold = Backend::make();
        auto& exec = hold.get();

        typename Backend::tcp_listener_t listener {exec, loopback(), 0u};
        const auto port = listen_on_ephemeral_port(listener, 8);
        if (port == 0u)
            return skipped(std::move(name), "the listener could not be bound");

        std::atomic_size_t received_blocks {};
        detail::run_window window {};
        tls_stream_list_t<Backend> server_sessions {};

        // The sending session outlives the coroutine that writes through it. Held in the coroutine's
        // own frame instead, it was destroyed the moment the last block was handed over - closing the
        // socket under whatever was still in flight, and costing the receiver the last several hundred
        // kilobytes. The case then divided a full window by a short count and reported a per-block
        // figure that was quietly wrong.
        tls_stream_list_t<Backend> client_sessions {};

        exec.spawn(detail::tls_block_sink<Backend>({.exec = exec,
                                                    .listener = listener,
                                                    .ctx = contexts.server.get(),
                                                    .count = blocks,
                                                    .size = block_size,
                                                    .counter = received_blocks,
                                                    .window = window,
                                                    .keep_alive = server_sessions}));
        exec.spawn(detail::tls_block_source<Backend>({.exec = exec,
                                                      .port = port,
                                                      .ctx = contexts.client.get(),
                                                      .count = blocks,
                                                      .size = block_size,
                                                      .window = window,
                                                      .keep_alive = client_sessions}));

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

}
