/// @file aio/benchmark/feature/tls_scenarios.hpp
/// @brief TLS scenarios, written once and measured on both execution models.
/// @details tls::stream is already a template over the stream underneath it, so the two models share
///          the whole TLS layer - the handshake, the record loops, the BIO pumping - and differ only
///          in the tcp::stream at the bottom. That makes these the cleanest pairings in the suite:
///          whatever the delta is, it is the transport, because there is nothing else it could be.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <atomic>
    #include <cstddef>
    #include <filesystem>
    #include <memory>
    #include <string>
    #include <vector>

    #include <openssl/ssl.h>

    #include <kmx/aio/benchmark/feature/scenarios.hpp>
    #include <kmx/aio/test/tls_certs.hpp>
    #include <kmx/aio/tls/stream.hpp>
#endif

namespace kmx::aio::benchmark::feature
{
    namespace catalogue
    {
        /// @brief The TLS handshake scenario.
        struct tls_handshake_scenario
        {
            static constexpr std::string_view key = "tls_handshake"; ///< The pairing key.
            static constexpr std::string_view description = "a full TLS 1.3 handshake over a fresh loopback TCP connection";
            static constexpr std::size_t iterations = 500u; ///< Handshakes timed at scale 1.
        };

        /// @brief The TLS record round-trip scenario.
        struct tls_echo_scenario
        {
            static constexpr std::string_view key = "tls_echo_rtt"; ///< The pairing key.
            static constexpr std::string_view description = "64 bytes out and back through an established TLS session";
            static constexpr std::size_t iterations = 5'000u; ///< Round trips at scale 1.
            static constexpr std::size_t payload_size = 64u;  ///< Bytes per round trip.
        };

        /// @brief The TLS bulk transfer scenario.
        struct tls_throughput_scenario
        {
            static constexpr std::string_view key = "tls_throughput (16 KiB)"; ///< The pairing key.
            static constexpr std::string_view description =
                "16 KiB blocks streamed one way through an established TLS session; the cost of one block";
            static constexpr std::size_t blocks = 4'000u;      ///< Blocks sent at scale 1.
            static constexpr std::size_t block_size = 16'384u; ///< Bytes per block. One TLS record's worth.
        };
    } // namespace catalogue

    namespace detail
    {
        /// @brief A self-signed certificate and its key, generated once for the whole run.
        /// @details Generating one is not what is being measured, and doing it per handshake would put
        ///          an openssl(1) fork into the middle of a benchmark. Reuses whatever is already on
        ///          disk from an earlier run.
        struct tls_credentials
        {
            std::string certificate; ///< Path to the certificate.
            std::string key;         ///< Path to the private key.
            bool usable {};          ///< False when openssl(1) could not produce them.
        };

        /// @brief Generates the run's certificate and key, reusing whatever an earlier run left on disk.
        /// @return The credentials, with usable false when they could not be made.
        [[nodiscard]] inline tls_credentials make_credentials() noexcept
        {
            const std::filesystem::path directory {"/tmp/kmx_aio_benchmark_certs"};
            const auto certificate = directory / "server_cert.pem";
            const auto key = directory / "server_key.pem";

            std::error_code ec;
            std::filesystem::create_directories(directory, ec);
            if (ec)
                return tls_credentials {certificate.string(), key.string(), false};

            const auto made = test::ensure_self_signed_pair(certificate, key, "localhost");
            return tls_credentials {certificate.string(), key.string(), made};
        }

        /// @brief Returns the run's credentials, generating them on first use.
        /// @return The credentials, with usable false when they could not be made.
        [[nodiscard]] inline const tls_credentials& shared_credentials() noexcept
        {
            static const tls_credentials credentials = make_credentials();
            return credentials;
        }

        /// @brief The two contexts a session needs, configured once per case.
        struct tls_contexts
        {
            test::scoped_ssl_ctx server {::TLS_server_method()}; ///< The accepting side's context.
            test::scoped_ssl_ctx client {::TLS_client_method()}; ///< The connecting side's context.

            /// @brief Loads the run's certificate into the server context and disables client verification.
            /// @return True when both contexts are usable.
            [[nodiscard]] bool configure() noexcept
            {
                const auto& credentials = shared_credentials();
                if (!credentials.usable || (server.get() == nullptr) || (client.get() == nullptr))
                    return false;

                if (::SSL_CTX_use_certificate_chain_file(server.get(), credentials.certificate.c_str()) != 1)
                    return false;

                if (::SSL_CTX_use_PrivateKey_file(server.get(), credentials.key.c_str(), SSL_FILETYPE_PEM) != 1)
                    return false;

                // The certificate is self-signed and the point of the case is the handshake's cost, not
                // whether a chain validates. Verifying it would measure a trust store that no two
                // machines running this have configured the same way.
                ::SSL_CTX_set_verify(client.get(), SSL_VERIFY_NONE, nullptr);
                return true;
            }
        };
    } // namespace detail

    /// @brief One TLS session, established over a fresh loopback TCP connection.
    /// @tparam Backend The execution model to drive.
    template <typename Backend>
    using tls_stream_t = kmx::aio::tls::stream<typename Backend::tcp_stream_t>;

    namespace detail
    {
        /// @brief The accepting side: take a connection, hand it a TLS session, handshake it.
        /// @tparam Backend The execution model to drive.
        /// @param exec The executor.
        /// @param listener The listening socket. Must outlive the run.
        /// @param ctx The server context. Must outlive the run.
        /// @param count How many sessions to accept.
        /// @param out Receives the established sessions, so they stay alive. Must outlive the run.
        /// @throws std::bad_alloc (coroutine frame allocation).
        template <typename Backend>
        task<void> tls_acceptor(typename Backend::executor_t& exec, typename Backend::tcp_listener_t& listener, ::SSL_CTX* const ctx,
                                const std::size_t count, std::vector<std::shared_ptr<tls_stream_t<Backend>>>& out) noexcept(false)
        {
            for (std::size_t i {}; i != count; ++i)
            {
                auto accepted = co_await listener.accept();
                if (!accepted)
                    co_return;

                auto session = std::make_shared<tls_stream_t<Backend>>(typename Backend::tcp_stream_t {exec, std::move(*accepted)}, ctx);
                session->set_accept_state();

                // Kept alive by the caller's vector: the handshake below suspends, and a session
                // destroyed while its coroutine is parked on the socket takes the socket with it.
                out.push_back(session);
                exec.spawn([](std::shared_ptr<tls_stream_t<Backend>> s) -> task<void> { co_await s->handshake(); }(session));
            }
        }

        /// @brief The connecting side of the handshake case: connect, handshake, time it, repeat.
        /// @tparam Backend The execution model to drive.
        /// @param exec The executor.
        /// @param port The loopback port to connect to.
        /// @param ctx The client context. Must outlive the run.
        /// @param count How many handshakes to time.
        /// @param samples One nanosecond figure appended per completed handshake.
        /// @throws std::bad_alloc (coroutine frame and sample allocation).
        template <typename Backend>
        task<void> tls_handshake_client(typename Backend::executor_t& exec, const port_t port, ::SSL_CTX* const ctx,
                                        const std::size_t count, std::vector<double>& samples) noexcept(false)
        {
            for (std::size_t i {}; i != count; ++i)
            {
                auto connected = co_await Backend::connect(exec, port);
                if (!connected)
                    co_return;

                tls_stream_t<Backend> session {typename Backend::tcp_stream_t {exec, std::move(*connected)}, ctx};
                session.set_connect_state();

                const auto start = clock_t::now();
                if (!co_await session.handshake())
                    co_return;

                samples.push_back(
                    static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));
            }
        }

        /// @brief The accepting side of the echo case: one session, then read a block and write it back.
        /// @tparam Backend The execution model to drive.
        /// @param exec The executor.
        /// @param listener The listening socket. Must outlive the run.
        /// @param ctx The server context. Must outlive the run.
        /// @param count How many round trips to serve.
        /// @param size Bytes per round trip.
        /// @param keep_alive Holds the session, which outlives this coroutine's suspensions.
        /// @throws std::bad_alloc (coroutine frame and buffer allocation).
        template <typename Backend>
        task<void> tls_echo_server(typename Backend::executor_t& exec, typename Backend::tcp_listener_t& listener, ::SSL_CTX* const ctx,
                                   const std::size_t count, const std::size_t size,
                                   std::vector<std::shared_ptr<tls_stream_t<Backend>>>& keep_alive) noexcept(false)
        {
            auto accepted = co_await listener.accept();
            if (!accepted)
                co_return;

            auto session = std::make_shared<tls_stream_t<Backend>>(typename Backend::tcp_stream_t {exec, std::move(*accepted)}, ctx);
            keep_alive.push_back(session);
            session->set_accept_state();
            if (!co_await session->handshake())
                co_return;

            std::vector<char> buffer(size);
            for (std::size_t i {}; i != count; ++i)
            {
                if (!co_await stream_read_exact(*session, span_char_t(buffer.data(), buffer.size())))
                    co_return;

                if (!co_await session->write_all(cspan_char_t(buffer.data(), buffer.size())))
                    co_return;
            }
        }

        /// @brief The connecting side of the echo case: handshake once, then time each round trip.
        /// @tparam Backend The execution model to drive.
        /// @param exec The executor.
        /// @param port The loopback port to connect to.
        /// @param ctx The client context. Must outlive the run.
        /// @param count How many round trips to make.
        /// @param size Bytes per round trip.
        /// @param samples One nanosecond figure appended per round trip, or nullptr to record none.
        /// @param completed Incremented once per completed round trip.
        /// @param window Opened after the handshake and closed at the end, stamping the measured window.
        /// @throws std::bad_alloc (coroutine frame, buffer and sample allocation).
        template <typename Backend>
        task<void> tls_echo_client(typename Backend::executor_t& exec, const port_t port, ::SSL_CTX* const ctx, const std::size_t count,
                                   const std::size_t size, std::vector<double>* const samples, std::atomic_size_t& completed,
                                   run_window& window) noexcept(false)
        {
            auto connected = co_await Backend::connect(exec, port);
            if (!connected)
                co_return;

            tls_stream_t<Backend> session {typename Backend::tcp_stream_t {exec, std::move(*connected)}, ctx};
            session.set_connect_state();
            if (!co_await session.handshake())
                co_return;

            std::vector<char> buffer(size);

            // Opened after the handshake: this case is about the record layer, and averaging one
            // handshake over a few thousand round trips would quietly add it to every one of them.
            window.open();

            for (std::size_t i {}; i != count; ++i)
            {
                const auto start = clock_t::now();

                if (!co_await session.write_all(cspan_char_t(buffer.data(), buffer.size())))
                    break;

                if (!co_await stream_read_exact(session, span_char_t(buffer.data(), buffer.size())))
                    break;

                if (samples != nullptr)
                    samples->push_back(
                        static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));

                completed.fetch_add(1u, std::memory_order_relaxed);
            }

            window.close(1u);
        }

        /// @brief The receiving side of the throughput case: one session, then count whole blocks.
        /// @tparam Backend The execution model to drive.
        /// @param exec The executor.
        /// @param listener The listening socket. Must outlive the run.
        /// @param ctx The server context. Must outlive the run.
        /// @param count How many blocks to read.
        /// @param size Bytes per block.
        /// @param counter Incremented once per whole block read.
        /// @param window Closed once the sink is done, stamping the measured window.
        /// @param keep_alive Holds the session, which outlives this coroutine's suspensions.
        /// @throws std::bad_alloc (coroutine frame and buffer allocation).
        template <typename Backend>
        task<void> tls_block_sink(typename Backend::executor_t& exec, typename Backend::tcp_listener_t& listener, ::SSL_CTX* const ctx,
                                  const std::size_t count, const std::size_t size, std::atomic_size_t& counter, run_window& window,
                                  std::vector<std::shared_ptr<tls_stream_t<Backend>>>& keep_alive) noexcept(false)
        {
            auto accepted = co_await listener.accept();
            if (!accepted)
                co_return;

            auto session = std::make_shared<tls_stream_t<Backend>>(typename Backend::tcp_stream_t {exec, std::move(*accepted)}, ctx);
            keep_alive.push_back(session);
            session->set_accept_state();
            if (!co_await session->handshake())
            {
                window.close(1u);
                co_return;
            }

            std::vector<char> buffer(size);
            for (std::size_t i {}; i != count; ++i)
            {
                if (!co_await stream_read_exact(*session, span_char_t(buffer.data(), buffer.size())))
                    break;

                counter.fetch_add(1u, std::memory_order_relaxed);
            }

            window.close(1u);
        }

        /// @brief The sending side of the throughput case: handshake once, then stream blocks one way.
        /// @tparam Backend The execution model to drive.
        /// @param exec The executor.
        /// @param port The loopback port to connect to.
        /// @param ctx The client context. Must outlive the run.
        /// @param count How many blocks to write.
        /// @param size Bytes per block.
        /// @param window Opened after the handshake, so the asymmetric crypto is not spread over the blocks.
        /// @param keep_alive Holds the session, which must outlive the blocks still in flight.
        /// @throws std::bad_alloc (coroutine frame and buffer allocation).
        template <typename Backend>
        task<void> tls_block_source(typename Backend::executor_t& exec, const port_t port, ::SSL_CTX* const ctx, const std::size_t count,
                                    const std::size_t size, run_window& window,
                                    std::vector<std::shared_ptr<tls_stream_t<Backend>>>& keep_alive) noexcept(false)
        {
            auto connected = co_await Backend::connect(exec, port);
            if (!connected)
                co_return;

            auto session = std::make_shared<tls_stream_t<Backend>>(typename Backend::tcp_stream_t {exec, std::move(*connected)}, ctx);
            keep_alive.push_back(session);
            session->set_connect_state();
            if (!co_await session->handshake())
                co_return;

            const std::vector<char> buffer(size);

            window.open();
            for (std::size_t i {}; i != count; ++i)
                if (!co_await session->write_all(cspan_char_t(buffer.data(), buffer.size())))
                    break;
        }
    } // namespace detail

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
        std::vector<std::shared_ptr<tls_stream_t<Backend>>> server_sessions {};
        server_sessions.reserve(iterations);

        exec.spawn(detail::tls_acceptor<Backend>(exec, listener, contexts.server.get(), iterations, server_sessions));
        exec.spawn(detail::tls_handshake_client<Backend>(exec, port, contexts.client.get(), iterations, samples));

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
        std::vector<std::shared_ptr<tls_stream_t<Backend>>> server_sessions {};

        exec.spawn(detail::tls_echo_server<Backend>(exec, listener, contexts.server.get(), rounds, payload_size, server_sessions));
        exec.spawn(detail::tls_echo_client<Backend>(exec, port, contexts.client.get(), rounds, payload_size, &samples, completed, window));

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
        std::vector<std::shared_ptr<tls_stream_t<Backend>>> server_sessions {};

        // The sending session outlives the coroutine that writes through it. Held in the coroutine's
        // own frame instead, it was destroyed the moment the last block was handed over - closing the
        // socket under whatever was still in flight, and costing the receiver the last several hundred
        // kilobytes. The case then divided a full window by a short count and reported a per-block
        // figure that was quietly wrong.
        std::vector<std::shared_ptr<tls_stream_t<Backend>>> client_sessions {};

        exec.spawn(
            detail::tls_block_sink<Backend>(exec, listener, contexts.server.get(), blocks, block_size, received_blocks, window,
                                            server_sessions));
        exec.spawn(detail::tls_block_source<Backend>(exec, port, contexts.client.get(), blocks, block_size, window, client_sessions));

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

} // namespace kmx::aio::benchmark::feature
