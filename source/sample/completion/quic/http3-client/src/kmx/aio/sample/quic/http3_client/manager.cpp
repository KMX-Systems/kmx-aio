#include <kmx/aio/sample/quic/http3_client/manager.hpp>

#include <array>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <kmx/aio/completion/quic/engine.hpp>
#include <lsquic.h>
#include <memory>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string>
#include <string_view>

namespace kmx::aio::sample::quic::http3_client
{
    using namespace kmx::aio;
    using namespace kmx::aio::completion;

    namespace detail
    {
        std::uint16_t parse_peer_port_from_env()        {
            constexpr std::uint16_t default_port = 12345u;
            const char* const env = std::getenv("KMX_QUIC_HTTP3_PORT");
            if (!env || env[0] == '\0')
                return default_port;

            std::uint32_t parsed {};
            const char* const end = env + std::char_traits<char>::length(env);
            const auto [ptr, ec] = std::from_chars(env, end, parsed);
            if (ec != std::errc() || ptr != end || parsed == 0u || parsed > 65535u)
                return default_port;

            return static_cast<std::uint16_t>(parsed);
        }
    }

    task<void> handle_stream(::lsquic_stream_t* stream, kmx::aio::quic::stream_payload payload)
    {
        auto data = payload.bytes();
        std::string_view response(data.data(), data.size());
        std::cout << "\n[HTTP/3 Client] Received Server Response:\n"
                  << "--------------------------------------------------------\n"
                  << response << "\n"
                  << "--------------------------------------------------------\n";

        ::lsquic_conn_close(::lsquic_stream_conn(stream));
        co_return; // No further action needed.
    }

    task<void> async_main(std::shared_ptr<executor> exec)    {
        // Client SSL setup
        ::SSL_CTX* ssl_ctx = ::SSL_CTX_new(TLS_client_method());
        if (!ssl_ctx)
        {
            std::cerr << "Failed to create client SSL_CTX\n";
            co_return;
        }
        static constexpr std::array<unsigned char, 8u> kmx_alpn_wire = {7u, 'k', 'm', 'x', '-', 'a', 'i', 'o'};
        if (::SSL_CTX_set_alpn_protos(ssl_ctx, kmx_alpn_wire.data(), static_cast<unsigned int>(kmx_alpn_wire.size())) != 0)
        {
            std::cerr << "Failed to configure client ALPN\n";
            ::SSL_CTX_free(ssl_ctx);
            co_return;
        }
        // For testing purposes, we might not verify the cert directly.
        ::SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, nullptr);

        kmx::aio::completion::quic::engine engine(*exec);
        // stream handler is used when server writes back to the stream we created
        engine.set_stream_handler(handle_stream);

        // Dial the server
        std::string payload = "GET / HTTP/0.9\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        static constexpr std::array<unsigned char, 4> peer_ip = {127, 0, 0, 1};
        const std::uint16_t peer_port = detail::parse_peer_port_from_env();

        std::cout << "[HTTP/3 Client] Connecting to 127.0.0.1:" << peer_port << "...\n";

        auto res = co_await engine.connect(peer_ip, peer_port, "localhost", payload, ssl_ctx);
        if (!res)
        {
            std::cerr << "Failed to connect engine: " << res.error().message() << "\n";
            ::SSL_CTX_free(ssl_ctx);
            co_return;
        }

        std::cout << "[HTTP/3 Client] Processing events...\n";

        auto process_res = co_await engine.process();
        if (!process_res)
            std::cerr << "Engine process error: " << process_res.error().message() << "\n";

        std::cout << "[HTTP/3 Client] Exiting.\n";
        ::SSL_CTX_free(ssl_ctx);
    }
}
