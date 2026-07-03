#include <kmx/aio/sample/quic/http3_client/manager.hpp>

#include <array>
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

    auto async_main(std::shared_ptr<executor> exec) -> task<void>
    {
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
        static constexpr std::uint16_t peer_port = 12345;

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
