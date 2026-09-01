/// @file aio/tls/duplex_test.cpp
/// @brief The concurrency contract of tls::basic_stream, exercised over a live session.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// basic_stream promises that one reader and one writer may run at once on the same stream, from
/// different threads. Nothing in stream_test.cpp reaches that promise: those tests never complete a
/// handshake, so no ::SSL there ever encrypts a record, and the locks the promise rests on are never
/// contended.
///
/// So this drives a real session - two TLS streams over a socketpair, handshaken against each other -
/// on a readiness executor with four threads, and then reads and writes it from two coroutines at once.
/// That is the exact shape that used to crash inside SSL_read: the reader and the writer are resumed
/// on different scheduler workers and walk into the same ::SSL. A regression here is a segfault or a
/// truncated transfer, not a quiet wrong answer.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <sys/socket.h>

#if defined(KMX_AIO_FEATURE_READINESS)
    #include <openssl/ssl.h>

    #include <kmx/aio/file_descriptor.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/readiness/tcp/stream.hpp>
    #include <kmx/aio/task.hpp>
    #include <kmx/aio/test/executor_runner.hpp>
    #include <kmx/aio/test/tls_certs.hpp>
    #include <kmx/aio/tls/stream.hpp>

namespace kmx::aio::test::tls::duplex_test
{
    using namespace kmx::aio::tls;

    namespace detail
    {
        using kmx::aio::test::scoped_runner;
        using kmx::aio::test::wait_for_flag;

        using tls_stream = stream<readiness::tcp::stream>;

        /// @brief How much the client sends, and expects to get back.
        /// @details Larger than a socket buffer on purpose. A total that fits in the kernel's buffers
        ///          would let the writer finish before the reader ever ran, and the test would pass
        ///          without the two ever having overlapped - which is the only thing it is here to
        ///          check.
        constexpr std::size_t total_bytes {256u * 1024u};
        /// @brief The size of one write.
        /// @details Small on purpose. The transfer is a fixed number of bytes either way, so a smaller
        ///          chunk means more records, more trips through the TLS layer, and more chances for
        ///          the reader and the writer to be inside it at the same moment - which is what the
        ///          test is trying to provoke. At 512 bytes an unsynchronised session fails several
        ///          runs out of five; at 4096 it survived two out of three.
        constexpr std::size_t chunk_bytes {512u};
        /// @brief How long the transfer is given before the test calls it hung.
        constexpr std::chrono::milliseconds transfer_deadline {60000};

        /// @brief A self-signed certificate and its key, generated once per run.
        struct server_credentials
        {
            std::string certificate;
            std::string key;
            bool usable {};
        };

        /// @brief Generates a self-signed certificate under /tmp, reusing one already there.
        /// @return The paths, with usable set to false when openssl(1) could not produce them.
        [[nodiscard]] const server_credentials& shared_credentials()
        {
            static const server_credentials credentials = []
            {
                const std::filesystem::path directory {"/tmp/kmx_tls_duplex_certs"};
                const auto certificate = (directory / "server_cert.pem").string();
                const auto key = (directory / "server_key.pem").string();

                if (std::filesystem::exists(certificate) && std::filesystem::exists(key))
                    return server_credentials {certificate, key, true};

                std::error_code ec;
                std::filesystem::create_directories(directory, ec);
                if (ec)
                    return server_credentials {certificate, key, false};

                const auto command = "openssl req -x509 -newkey rsa:2048 -keyout " + key + " -out " + certificate +
                                     " -days 30 -nodes -subj '/CN=localhost' >/dev/null 2>&1";

                return server_credentials {certificate, key, std::system(command.c_str()) == 0};
            }();

            return credentials;
        }

        /// @brief Echoes back whatever arrives, until the peer stops sending.
        /// @param server The accepting side of the session.
        /// @param handshaken Set once the handshake completed, so the test can report which step failed.
        /// @param finished Set when the echo loop has ended, however it ended.
        [[nodiscard]] task<void> echo_side(std::shared_ptr<tls_stream> server, std::atomic_bool& handshaken,
                                           std::atomic_bool& finished) noexcept(false)
        {
            server->set_accept_state();
            if (const auto result = co_await server->handshake(); result)
                handshaken.store(true, std::memory_order_release);
            else
            {
                finished.store(true, std::memory_order_release);
                co_return;
            }

            std::vector<char> buffer(chunk_bytes);
            while (true)
            {
                const auto received = co_await server->read(buffer);
                if (!received || (*received == 0u))
                    break;

                if (const auto sent = co_await server->write_all(std::span {buffer.data(), *received}); !sent)
                    break;
            }

            finished.store(true, std::memory_order_release);
        }

        /// @brief Writes the whole payload, in chunks, on its own coroutine.
        /// @param client The connecting side of the session.
        /// @param sent Total bytes handed to the TLS layer.
        /// @param finished Set when the writer has stopped, however it stopped.
        [[nodiscard]] task<void> writer_side(std::shared_ptr<tls_stream> client, std::atomic_size_t& sent,
                                             std::atomic_bool& finished) noexcept(false)
        {
            const std::vector<char> payload(chunk_bytes, 'k');
            while (sent.load(std::memory_order_relaxed) < total_bytes)
            {
                if (const auto result = co_await client->write_all(payload); !result)
                    break;

                sent.fetch_add(payload.size(), std::memory_order_relaxed);
            }

            finished.store(true, std::memory_order_release);
        }

        /// @brief Handshakes, starts the writer, and reads the echo back on this coroutine.
        /// @details The writer is spawned rather than awaited, so from here on two coroutines are live
        ///          on one ::SSL - which is the arrangement under test.
        /// @param client The connecting side of the session.
        /// @param exec The executor the writer is spawned into.
        /// @param sent Total bytes the writer handed over.
        /// @param received Total bytes read back.
        /// @param handshaken Set once the handshake completed.
        /// @param finished Set when the reader has stopped.
        [[nodiscard]] task<void> duplex_side(std::shared_ptr<tls_stream> client, readiness::executor& exec, std::atomic_size_t& sent,
                                             std::atomic_size_t& received, std::atomic_bool& handshaken,
                                             std::atomic_bool& finished) noexcept(false)
        {
            client->set_connect_state();
            if (const auto result = co_await client->handshake(); result)
                handshaken.store(true, std::memory_order_release);
            else
            {
                finished.store(true, std::memory_order_release);
                co_return;
            }

            std::atomic_bool writer_finished {false};
            exec.spawn(writer_side(client, sent, writer_finished));

            std::vector<char> buffer(chunk_bytes);
            while (received.load(std::memory_order_relaxed) < total_bytes)
            {
                const auto count = co_await client->read(buffer);
                if (!count || (*count == 0u))
                    break;

                received.fetch_add(*count, std::memory_order_relaxed);
            }

            // The writer holds a reference to writer_finished, which lives in this frame, so this
            // coroutine must outlive it.
            while (!writer_finished.load(std::memory_order_acquire))
                static_cast<void>(co_await exec.async_timeout(1u));

            finished.store(true, std::memory_order_release);
        }
    } // namespace detail

    TEST_CASE("a TLS session carries a read and a write at the same time", "[readiness][tls][stream][duplex][slow]")
    {
        const auto& credentials = detail::shared_credentials();
        if (!credentials.usable)
            SKIP("openssl(1) could not generate a server certificate");

        const scoped_ssl_ctx server_ctx {::TLS_server_method()};
        const scoped_ssl_ctx client_ctx {::TLS_client_method()};
        REQUIRE(server_ctx.get() != nullptr);
        REQUIRE(client_ctx.get() != nullptr);

        REQUIRE(::SSL_CTX_use_certificate_chain_file(server_ctx.get(), credentials.certificate.c_str()) == 1);
        REQUIRE(::SSL_CTX_use_PrivateKey_file(server_ctx.get(), credentials.key.c_str(), SSL_FILETYPE_PEM) == 1);
        ::SSL_CTX_set_verify(client_ctx.get(), SSL_VERIFY_NONE, nullptr);

        int fds[2] {-1, -1};
        REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds) == 0);

        // Four threads is what makes this a concurrency test: the readiness executor sizes its
        // scheduler from the same count, so the reader and the writer are resumed on different workers.
        // Shared rather than automatic: the executor keeps itself alive across a resumption through
        // shared_from_this(), so one built on the stack throws bad_weak_ptr at the first I/O wait.
        const readiness::executor_config config {.thread_count = 4u, .max_events = 64u, .timeout_ms = 20u};
        const auto exec = std::make_shared<readiness::executor>(config);

        std::atomic_size_t sent {};
        std::atomic_size_t received {};
        std::atomic_bool server_handshaken {false};
        std::atomic_bool client_handshaken {false};
        std::atomic_bool server_finished {false};
        std::atomic_bool client_finished {false};

        // io_base unregisters on destruction but never registers - arming the descriptor is the
        // caller's job, and a stream whose descriptor was never added to epoll waits forever.
        REQUIRE(exec->register_fd(fds[0]).has_value());
        REQUIRE(exec->register_fd(fds[1]).has_value());

        {
            auto server = std::make_shared<detail::tls_stream>(readiness::tcp::stream {*exec, file_descriptor {fds[0]}}, server_ctx.get());
            auto client = std::make_shared<detail::tls_stream>(readiness::tcp::stream {*exec, file_descriptor {fds[1]}}, client_ctx.get());

            exec->spawn(detail::echo_side(std::move(server), server_handshaken, server_finished));
            exec->spawn(detail::duplex_side(std::move(client), *exec, sent, received, client_handshaken, client_finished));

            const scoped_runner runner {*exec};
            CHECK(wait_for_flag(client_finished, detail::transfer_deadline));
        }

        CHECK(server_handshaken.load(std::memory_order_acquire));
        CHECK(client_handshaken.load(std::memory_order_acquire));

        // Every byte written came back. A session that lost the race inside OpenSSL either dies here or
        // returns short, and both are failures rather than flakes.
        CHECK(sent.load(std::memory_order_relaxed) == detail::total_bytes);
        CHECK(received.load(std::memory_order_relaxed) == detail::total_bytes);
    }
} // namespace kmx::aio::test::tls::duplex_test
#endif // KMX_AIO_FEATURE_READINESS
