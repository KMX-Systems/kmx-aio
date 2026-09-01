/// @file aio/tls/stream_test.cpp
/// @brief Unit tests for the TLS stream's construction, ALPN configuration and teardown.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// The existing TLS coverage all runs through the [integration] handshake tests, which need a
/// certificate pair and a live socket. Everything below needs neither: tls::stream is a template over
/// the stream underneath it, and the parts that have nothing to do with the handshake - the SSL and BIO
/// ownership, ALPN configuration, the accessors - can be driven against a stub inner stream.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <new>
#include <span>
#include <system_error>
#include <utility>

#include <openssl/ssl.h>
#include <sys/socket.h>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/tcp/stream.hpp>
#include <kmx/aio/file_descriptor.hpp>
#include <kmx/aio/tls/stream.hpp>

// The readiness instantiation is only linkable when the readiness library is part of the build.
#if defined(KMX_AIO_FEATURE_READINESS)
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/readiness/tcp/stream.hpp>
#endif
#include <kmx/aio/test/tls_certs.hpp>

namespace kmx::aio::test::tls::stream_test
{
    using namespace kmx::aio::tls;

    namespace detail
    {
        /// @brief The least an inner stream has to be for tls::stream to hold one.
        /// @details tls::stream stores it, hands it back through next_layer(), and forwards the two
        ///          calls basic_stream pumps bytes through to it. Those two are what makes an inner
        ///          stream an inner stream, and tls::stream overrides basic_stream's virtuals with
        ///          them, so a stub without them does not compile - but nothing below reaches them:
        ///          the paths that pump are the handshake's business, and those are covered by the
        ///          integration tests.
        struct stub_stream
        {
            int id {};

            /// @brief Stands in for a transport read. Never reached by the tests below.
            [[nodiscard]] task_returning_expected_size_t read(std::span<char>) noexcept(false)
            {
                co_return std::unexpected(std::make_error_code(std::errc::not_supported));
            }

            /// @brief Stands in for a transport write. Never reached by the tests below.
            [[nodiscard]] task_returning_expected_void_t write_all(cspan_char_t) noexcept(false)
            {
                co_return std::unexpected(std::make_error_code(std::errc::not_supported));
            }
        };

        // ALPN is configured in wire format: each entry is a length byte followed by that many bytes of
        // protocol name. "h2" and "http/1.1", the two this library's samples negotiate.
        constexpr std::array<std::uint8_t, 3u> alpn_h2 {2u, 'h', '2'};
        constexpr std::array<std::uint8_t, 12u> alpn_h2_and_http11 {2u, 'h', '2', 8u, 'h', 't', 't', 'p', '/', '1', '.', '1'};
    } // namespace detail

    TEST_CASE("a TLS stream takes ownership of an SSL and its BIOs", "[core][tls][stream]")
    {
        const scoped_ssl_ctx ctx;
        REQUIRE(ctx.get() != nullptr);

        // The destructor frees the SSL, and the SSL frees the two memory BIOs attached to it. Nothing
        // here asserts that directly - a leak is what the sanitiser build is for - but the construct and
        // destroy pair has to run at all before either can be trusted.
        {
            stream<detail::stub_stream> tls_stream {detail::stub_stream {7}, ctx.get()};
            REQUIRE(tls_stream.next_layer() != nullptr);
            CHECK(tls_stream.next_layer()->id == 7);
        }

        SUCCEED("the stream constructed and released its SSL");
    }

    TEST_CASE("a default-constructed TLS stream owns nothing", "[core][tls][stream]")
    {
        // The destructor's null guard: a stream that never got an SSL must not free one.
        stream<detail::stub_stream> tls_stream;
        CHECK(tls_stream.next_layer() == nullptr);
    }

    TEST_CASE("next_layer reports the inner stream through a const holder", "[core][tls][stream]")
    {
        const scoped_ssl_ctx ctx;
        REQUIRE(ctx.get() != nullptr);

        const stream<detail::stub_stream> tls_stream {detail::stub_stream {11}, ctx.get()};
        REQUIRE(tls_stream.next_layer() != nullptr);
        CHECK(tls_stream.next_layer()->id == 11);

        const stream<detail::stub_stream> empty;
        CHECK(empty.next_layer() == nullptr);
    }

    TEST_CASE("moving a TLS stream transfers the SSL", "[core][tls][stream][move]")
    {
        const scoped_ssl_ctx ctx;
        REQUIRE(ctx.get() != nullptr);

        stream<detail::stub_stream> source {detail::stub_stream {3}, ctx.get()};
        stream<detail::stub_stream> target {std::move(source)};

        REQUIRE(target.next_layer() != nullptr);
        CHECK(target.next_layer()->id == 3);

        // The SSL and both BIOs are exchanged out of the source, so the destructor that runs at the end
        // of this scope frees them once, from target. There is no accessor for the SSL to assert that
        // through - the observable is the absence of a double free, which the sanitiser build is what
        // catches.
        //
        // next_layer() is deliberately not part of that check: inner_ is a std::optional, and moving an
        // optional leaves the source engaged with a moved-from value rather than empty. A moved-from
        // stream therefore still hands back a pointer, which is worth knowing before writing a
        // null-check against it.
        CHECK(source.next_layer() != nullptr);
    }

    TEST_CASE("set_alpn_protocols accepts a wire-format list", "[core][tls][stream][alpn]")
    {
        const scoped_ssl_ctx ctx;
        REQUIRE(ctx.get() != nullptr);

        stream<detail::stub_stream> tls_stream {detail::stub_stream {}, ctx.get()};
        CHECK(tls_stream.set_alpn_protocols(cspan_uint8_t(detail::alpn_h2)).has_value());
        CHECK(tls_stream.set_alpn_protocols(cspan_uint8_t(detail::alpn_h2_and_http11)).has_value());
    }

    TEST_CASE("set_alpn_protocols rejects an empty list", "[core][tls][stream][alpn]")
    {
        const scoped_ssl_ctx ctx;
        REQUIRE(ctx.get() != nullptr);

        stream<detail::stub_stream> tls_stream {detail::stub_stream {}, ctx.get()};

        const auto result = tls_stream.set_alpn_protocols(cspan_uint8_t {});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::invalid_argument);
    }

    TEST_CASE("set_alpn_protocols rejects a stream with no SSL", "[core][tls][stream][alpn]")
    {
        // The other half of the same guard: a default-constructed stream has nothing to configure, and
        // passing its null SSL to OpenSSL would fault rather than fail.
        stream<detail::stub_stream> tls_stream;

        const auto result = tls_stream.set_alpn_protocols(cspan_uint8_t(detail::alpn_h2));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::invalid_argument);
    }

    TEST_CASE("selected_alpn is empty before a handshake", "[core][tls][stream][alpn]")
    {
        const scoped_ssl_ctx ctx;
        REQUIRE(ctx.get() != nullptr);

        stream<detail::stub_stream> tls_stream {detail::stub_stream {}, ctx.get()};
        REQUIRE(tls_stream.set_alpn_protocols(cspan_uint8_t(detail::alpn_h2)).has_value());

        // Offering a protocol is not negotiating one: nothing is selected until a peer has agreed.
        CHECK(tls_stream.selected_alpn().empty());
    }

    TEST_CASE("connect and accept states are settable", "[core][tls][stream]")
    {
        const scoped_ssl_ctx ctx;
        REQUIRE(ctx.get() != nullptr);

        stream<detail::stub_stream> client {detail::stub_stream {}, ctx.get()};
        client.set_connect_state();

        stream<detail::stub_stream> server {detail::stub_stream {}, ctx.get()};
        server.set_accept_state();

        SUCCEED("both handshake roles configured");
    }

    TEST_CASE("constructing without an SSL_CTX reports allocation failure", "[core][tls][stream][error]")
    {
        // SSL_new answers a null context with a null SSL, and the constructor has nothing to wrap; it
        // throws rather than leave a stream whose every later call would fault on a null SSL.
        CHECK_THROWS_AS((stream<detail::stub_stream> {detail::stub_stream {}, nullptr}), std::bad_alloc);
    }

    TEST_CASE("set_alpn_protocols rejects a malformed wire list", "[core][tls][stream][alpn][error]")
    {
        const scoped_ssl_ctx ctx;
        REQUIRE(ctx.get() != nullptr);

        stream<detail::stub_stream> tls_stream {detail::stub_stream {}, ctx.get()};

        // A length byte that runs past the end of the buffer, and a zero-length entry: OpenSSL rejects
        // both, and the wrapper has to report that rather than let a stream negotiate with a list the
        // library already refused.
        constexpr std::array<std::uint8_t, 3u> overrunning_length {9u, 'h', '2'};
        const auto overrun = tls_stream.set_alpn_protocols(cspan_uint8_t(overrunning_length));
        REQUIRE_FALSE(overrun.has_value());
        CHECK(overrun.error() == std::errc::protocol_error);

        constexpr std::array<std::uint8_t, 3u> zero_length_entry {0u, 'h', '2'};
        const auto zero_length = tls_stream.set_alpn_protocols(cspan_uint8_t(zero_length_entry));
        REQUIRE_FALSE(zero_length.has_value());
        CHECK(zero_length.error() == std::errc::protocol_error);
    }

    // the instantiations the library actually ships
    //
    // Everything above exercises tls::stream<stub_stream>. A class template is compiled once per
    // argument, so covering that instantiation says nothing about tls::stream<completion::tcp::stream>
    // or tls::stream<readiness::tcp::stream> - the two the samples and the handshake tests use, and the
    // two whose destructors and ALPN configuration were reported uncovered. They are instantiated here
    // over a real socket so that the code the library ships is the code under test.

    TEST_CASE("a TLS stream over a completion TCP stream owns and releases its SSL", "[core][tls][stream][instantiation]")
    {
        const scoped_ssl_ctx ctx;
        REQUIRE(ctx.get() != nullptr);

        auto socket = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(socket.has_value());

        completion::executor exec;
        {
            stream<completion::tcp::stream> tls_stream {completion::tcp::stream {exec, std::move(*socket)}, ctx.get()};
            REQUIRE(tls_stream.next_layer() != nullptr);
            CHECK(tls_stream.set_alpn_protocols(cspan_uint8_t(detail::alpn_h2)).has_value());
        }

        SUCCEED("the completion instantiation constructed, configured ALPN and released its SSL");
    }

#if defined(KMX_AIO_FEATURE_READINESS)
    TEST_CASE("a TLS stream over a readiness TCP stream owns and releases its SSL", "[core][tls][stream][instantiation]")
    {
        const scoped_ssl_ctx ctx;
        REQUIRE(ctx.get() != nullptr);

        auto socket = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(socket.has_value());

        readiness::executor exec;
        {
            stream<readiness::tcp::stream> tls_stream {readiness::tcp::stream {exec, std::move(*socket)}, ctx.get()};
            REQUIRE(tls_stream.next_layer() != nullptr);
            CHECK(tls_stream.set_alpn_protocols(cspan_uint8_t(detail::alpn_h2)).has_value());
        }

        SUCCEED("the readiness instantiation constructed, configured ALPN and released its SSL");
    }
#endif // KMX_AIO_FEATURE_READINESS

    TEST_CASE("the shipped instantiations reject a malformed ALPN list", "[core][tls][stream][instantiation][error]")
    {
        const scoped_ssl_ctx ctx;
        REQUIRE(ctx.get() != nullptr);
        constexpr std::array<std::uint8_t, 3u> overrunning_length {9u, 'h', '2'};

        auto completion_socket = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(completion_socket.has_value());
        completion::executor completion_exec;
        stream<completion::tcp::stream> over_completion {completion::tcp::stream {completion_exec, std::move(*completion_socket)},
                                                         ctx.get()};
        CHECK(over_completion.set_alpn_protocols(cspan_uint8_t(overrunning_length)).error() == std::errc::protocol_error);

#if defined(KMX_AIO_FEATURE_READINESS)
        auto readiness_socket = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(readiness_socket.has_value());
        readiness::executor readiness_exec;
        stream<readiness::tcp::stream> over_readiness {readiness::tcp::stream {readiness_exec, std::move(*readiness_socket)}, ctx.get()};
        CHECK(over_readiness.set_alpn_protocols(cspan_uint8_t(overrunning_length)).error() == std::errc::protocol_error);
#endif

        // A default-constructed stream of each shipped instantiation takes the destructor's null guard.
        const stream<completion::tcp::stream> empty_completion;
        CHECK(empty_completion.next_layer() == nullptr);
#if defined(KMX_AIO_FEATURE_READINESS)
        const stream<readiness::tcp::stream> empty_readiness;
        CHECK(empty_readiness.next_layer() == nullptr);
#endif
    }
} // namespace kmx::aio::test::tls::stream_test
