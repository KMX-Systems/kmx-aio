/// @file aio/quic/transport_test.cpp
/// @brief Loopback tests for quic::endpoint: a client and a server on one executor.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// @note These drive real handshakes over the loopback rather than mocking lsquic, because everything worth
///       testing here lives in the lsquic callbacks - which connection a stream belongs to, what survives a
///       connection ending - and none of that is reachable without one.
#if defined(KMX_AIO_FEATURE_QUIC)

    #include <catch2/catch_test_macros.hpp>

    #include <algorithm>
    #include <array>
    #include <cstdlib>
    #include <deque>
    #include <filesystem>
    #include <memory>
    #include <span>
    #include <string>
    #include <string_view>
    #include <vector>

    #include <openssl/ssl.h>

    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/quic/transport.hpp>
    #include <kmx/aio/task.hpp>

namespace kmx::aio::test::quic::transport_test
{
    using namespace kmx::aio::quic;

    namespace fs = std::filesystem;

    using executor = kmx::aio::completion::executor;
    using endpoint_t = endpoint<executor>;

    namespace detail
    {
        constexpr const char* test_alpn = "kmx-quic-test";
        constexpr std::array<std::uint8_t, 4u> loopback {127u, 0u, 0u, 1u};

        /// @brief A self-signed certificate, made once and left in /tmp for the next run.
        [[nodiscard]] bool ensure_certificates()
        {
            const fs::path cert = "/tmp/quic_cert.pem";
            const fs::path key = "/tmp/quic_key.pem";
            if (fs::exists(cert) && fs::exists(key))
                return true;

            const int rc = std::system("openssl req -x509 -newkey rsa:2048 -keyout /tmp/quic_key.pem -out /tmp/quic_cert.pem "
                                       "-days 1 -nodes -subj \"/CN=localhost\" >/dev/null 2>&1");
            return (rc == 0) && fs::exists(cert) && fs::exists(key);
        }

        /// @brief Owns an SSL_CTX for the duration of a test.
        class ssl_context
        {
        public:
            explicit ssl_context(::SSL_CTX* const ctx) noexcept: ctx_(ctx) {}
            ssl_context(const ssl_context&) = delete;
            ssl_context& operator=(const ssl_context&) = delete;

            ~ssl_context() noexcept
            {
                if (ctx_ != nullptr)
                    ::SSL_CTX_free(ctx_);
            }

            [[nodiscard]] ::SSL_CTX* get() const noexcept { return ctx_; }

        private:
            ::SSL_CTX* ctx_ {};
        };

        [[nodiscard]] std::shared_ptr<ssl_context> make_server_context()
        {
            auto* const ctx = ::SSL_CTX_new(TLS_method());
            if (ctx == nullptr)
                return {};

            auto owner = std::make_shared<ssl_context>(ctx);
            if (::SSL_CTX_use_certificate_chain_file(ctx, "/tmp/quic_cert.pem") != 1)
                return {};

            if (::SSL_CTX_use_PrivateKey_file(ctx, "/tmp/quic_key.pem", SSL_FILETYPE_PEM) != 1)
                return {};

            // Without this the handshake dies before a packet reaches the application; see transport.hpp.
            configure_server_alpn(ctx, test_alpn);
            return owner;
        }

        [[nodiscard]] std::shared_ptr<ssl_context> make_client_context()
        {
            auto* const ctx = ::SSL_CTX_new(TLS_method());
            if (ctx == nullptr)
                return {};

            // The certificate is self-signed; the point of the test is the transport, not the trust store.
            ::SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
            return std::make_shared<ssl_context>(ctx);
        }

        /// @brief Reads until @p expected bytes have arrived or the stream ends.
        [[nodiscard]] task<std::string> read_exactly(stream& s, const std::size_t expected) noexcept(false)
        {
            std::string collected;
            std::array<char, 256u> buffer {};
            while (collected.size() < expected)
            {
                const auto count = co_await s.read(std::span<char>(buffer));
                if (!count || (*count == 0u))
                    break;

                collected.append(buffer.data(), *count);
            }

            co_return collected;
        }

        /// @brief What one test run observed, filled in by the coroutines and checked once the loop ends.
        struct outcome
        {
            std::vector<std::string> server_received {};
            std::vector<std::string> client_received {};
            std::size_t accept_refusals {};
            bool server_open_rejected_when_idle {};
            std::error_code server_open_error {};
            bool server_open_rejected_after_close {};
            std::error_code server_open_after_close_error {};
            std::error_code failure {};
        };

        /// @brief Echoes every stream of every connection, for as long as the endpoint runs.
        /// @note Written the way the endpoint's documentation says a server must be: a refused accept is
        ///       transient - a connection ended, not the server - so the loop goes back to waiting. It leaves
        ///       only when the endpoint has stopped, which is what the failure after shutdown is for.
        task<void> serve(endpoint_t& server, std::shared_ptr<outcome> result) noexcept(false)
        {
            for (;;)
            {
                auto accepted = co_await server.accept_stream();
                if (!accepted)
                {
                    ++result->accept_refusals;
                    if (!server.is_running())
                        co_return;

                    continue;
                }

                auto peer_stream = std::move(*accepted);
                auto request = co_await read_exactly(peer_stream, 5u);
                result->server_received.push_back(request);

                const std::string response = "echo:" + request;
                (void) co_await peer_stream.write_all(cspan_char_t(response.data(), response.size()));
                peer_stream.shutdown_write();
            }
        }

        /// @brief One connection: open a stream, send @p payload, read the echo, close.
        task<void> exchange(executor& exec, const port_t port, std::string payload, std::shared_ptr<outcome> result,
                            std::shared_ptr<ssl_context> ctx) noexcept(false)
        {
            endpoint_t client(exec);
            client.set_alpn(test_alpn);

            const auto connected = client.connect(make_ip_address(loopback), port, "localhost", ctx->get());
            if (!connected)
            {
                result->failure = connected.error();
                co_return;
            }

            exec.spawn(client.run());

            auto opened = co_await client.session();
            if (!opened)
            {
                result->failure = opened.error();
                client.stop();
                co_return;
            }

            auto call = std::move(*opened);
            const auto written = co_await call.write_all(cspan_char_t(payload.data(), payload.size()));
            if (!written)
            {
                result->failure = written.error();
                client.stop();
                co_return;
            }

            result->client_received.push_back(co_await read_exactly(call, 10u));

            // Tell the server the connection is over, and keep the loop turning long enough to send it.
            client.close();
            for (unsigned i = 0u; i != 8u; ++i)
                (void) co_await exec.async_timeout(2u * 1000u * 1000u);

            client.stop();
        }

        /// @brief Two connections in turn against one server, and a look at the server between them.
        task<void> drive(executor& exec, std::shared_ptr<outcome> result, std::shared_ptr<ssl_context> server_ctx,
                         std::shared_ptr<ssl_context> client_ctx) noexcept(false)
        {
            endpoint_t server(exec);
            server.set_alpn(test_alpn);

            const auto listening = server.listen(make_ip_address(loopback), 0u, server_ctx->get());
            if (!listening)
            {
                result->failure = listening.error();
                exec.stop();
                co_return;
            }

            const port_t port = server.local_port();
            exec.spawn(server.run());
            exec.spawn(serve(server, result));

            // With no connection accepted yet, a server has nothing to open a stream on and must say so
            // rather than suspend for a peer that may never arrive.
            auto idle_open = co_await server.open_stream();
            result->server_open_rejected_when_idle = !idle_open.has_value();
            if (!idle_open)
                result->server_open_error = idle_open.error();

            co_await exchange(exec, port, "one--", result, client_ctx);

            // The regression: the first client has gone and taken its connection with it. A server that
            // treated that as its own end would be deaf from here on.
            co_await exchange(exec, port, "two--", result, client_ctx);

            // And with both connections gone the server is idle again, not holding one that lsquic has
            // freed. Getting this wrong is not a wrong answer but a use-after-free inside lsquic.
            auto stale_open = co_await server.open_stream();
            result->server_open_rejected_after_close = !stale_open.has_value();
            if (!stale_open)
                result->server_open_after_close_error = stale_open.error();

            server.stop();
            for (unsigned i = 0u; i != 4u; ++i)
                (void) co_await exec.async_timeout(2u * 1000u * 1000u);

            exec.stop();
        }
    } // namespace detail

    TEST_CASE("quic transport endpoint serves successive connections", "[quic][transport][integration]")
    {
        if (!detail::ensure_certificates())
            SKIP("QUIC transport test skipped: could not create /tmp/quic_cert.pem");

        const auto server_ctx = detail::make_server_context();
        const auto client_ctx = detail::make_client_context();
        if (!server_ctx || !client_ctx)
            SKIP("QUIC transport test skipped: could not configure OpenSSL contexts");

        executor exec;
        auto result = std::make_shared<detail::outcome>();

        exec.spawn(detail::drive(exec, result, server_ctx, client_ctx));
        exec.run();

        CHECK(result->failure.value() == 0);

        // A server with no connection refuses to open a stream instead of hanging - before it has accepted
        // one, and again once the ones it accepted have gone.
        CHECK(result->server_open_rejected_when_idle);
        CHECK(result->server_open_error == std::errc::not_connected);
        CHECK(result->server_open_rejected_after_close);
        CHECK(result->server_open_after_close_error == std::errc::not_connected);

        // Both connections were served, in order, by the one server.
        REQUIRE(result->server_received.size() == 2u);
        CHECK(result->server_received[0] == "one--");
        CHECK(result->server_received[1] == "two--");

        REQUIRE(result->client_received.size() == 2u);
        CHECK(result->client_received[0] == "echo:one--");
        CHECK(result->client_received[1] == "echo:two--");
    }
    /// @brief The buffer behind both directions of a stream.
    /// @note Worth testing on its own because its whole point is that consuming does not move anything, so
    ///       what a reader sees depends on a cursor and on when the storage behind it is reclaimed. Getting
    ///       that wrong reorders or drops bytes on a reliable stream, which is the one thing this layer
    ///       exists to prevent, and it would show up only as a protocol that desynchronises under load.
    TEST_CASE("quic byte_buffer behaves as a FIFO byte queue", "[quic][transport][unit]")
    {
        SECTION("a fresh buffer is empty")
        {
            byte_buffer buffer;
            CHECK(buffer.empty());
            CHECK(buffer.size() == 0u);
        }

        SECTION("appended bytes come back in order")
        {
            byte_buffer buffer;
            buffer.append("abcdef", 6u);

            REQUIRE(buffer.size() == 6u);
            CHECK(!buffer.empty());
            CHECK(std::string_view(buffer.data(), buffer.size()) == "abcdef");
        }

        SECTION("a partial consume leaves the remainder contiguous and in order")
        {
            byte_buffer buffer;
            buffer.append("abcdef", 6u);
            buffer.consume(2u);

            REQUIRE(buffer.size() == 4u);
            CHECK(std::string_view(buffer.data(), buffer.size()) == "cdef");
        }

        SECTION("consuming everything empties the buffer, which then refills from the front")
        {
            byte_buffer buffer;
            buffer.append("abc", 3u);
            buffer.consume(3u);

            CHECK(buffer.empty());
            CHECK(buffer.size() == 0u);

            buffer.append("xy", 2u);
            REQUIRE(buffer.size() == 2u);
            CHECK(std::string_view(buffer.data(), buffer.size()) == "xy");
        }

        SECTION("a consume past the half-way mark compacts without disturbing what is left")
        {
            // Five of eight consumed trips the (read_pos * 2 >= size) rule, so the remainder moves to the
            // front. What is queued must be exactly the same before and after that happens.
            byte_buffer buffer;
            buffer.append("01234567", 8u);
            buffer.consume(5u);

            REQUIRE(buffer.size() == 3u);
            CHECK(std::string_view(buffer.data(), buffer.size()) == "567");

            buffer.append("89", 2u);
            REQUIRE(buffer.size() == 5u);
            CHECK(std::string_view(buffer.data(), buffer.size()) == "56789");
        }

        SECTION("consuming nothing and appending nothing change nothing")
        {
            byte_buffer buffer;
            buffer.append("abc", 3u);
            buffer.consume(0u);
            buffer.append("", 0u);

            REQUIRE(buffer.size() == 3u);
            CHECK(std::string_view(buffer.data(), buffer.size()) == "abc");
        }

        SECTION("matches a reference FIFO across a long interleaved sequence")
        {
            // A std::deque<char> is the queue this replaced, so it is the definition of right. Driving both
            // with the same appends and consumes covers the orderings no hand-written case would think of -
            // in particular a compaction landing between an append and the read of what it moved.
            byte_buffer buffer;
            std::deque<char> reference;

            // Fixed seed: a failure has to be reproducible, and nothing here should depend on the run.
            std::uint64_t rng = 0x2545f4914f6cdd1dull;
            const auto next = [&rng](const std::size_t bound) noexcept
            {
                rng ^= rng << 13u;
                rng ^= rng >> 7u;
                rng ^= rng << 17u;
                return static_cast<std::size_t>(rng % bound);
            };

            std::size_t counter = 0u;
            bool sizes_matched = true;
            bool contents_matched = true;

            for (int step = 0; step != 4000; ++step)
            {
                if (reference.empty() || (next(2u) == 0u))
                {
                    std::vector<char> chunk(next(300u) + 1u);
                    for (auto& byte: chunk)
                    {
                        byte = static_cast<char>(counter & 0xffu);
                        ++counter;
                    }

                    buffer.append(chunk.data(), chunk.size());
                    reference.insert(reference.end(), chunk.begin(), chunk.end());
                }
                else
                {
                    const auto count = next(reference.size()) + 1u;
                    buffer.consume(count);
                    reference.erase(reference.begin(), reference.begin() + static_cast<std::ptrdiff_t>(count));
                }

                if (buffer.size() != reference.size())
                {
                    sizes_matched = false;
                    break;
                }

                if (!std::equal(reference.begin(), reference.end(), buffer.data(), buffer.data() + buffer.size()))
                {
                    contents_matched = false;
                    break;
                }
            }

            CHECK(sizes_matched);
            CHECK(contents_matched);
        }
    }

} // namespace kmx::aio::test::quic::transport_test

#endif // KMX_AIO_FEATURE_QUIC
