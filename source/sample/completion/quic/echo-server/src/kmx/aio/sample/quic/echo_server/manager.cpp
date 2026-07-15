#include <kmx/aio/sample/quic/echo_server/manager.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <iostream>
#include <kmx/aio/completion/quic/engine.hpp>
#include <lsquic.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string>
#include <string_view>

namespace kmx::aio::sample::quic::echo_server
{
    using namespace kmx::aio;
    using namespace kmx::aio::completion;

    static int select_kmx_alpn(::SSL* /*ssl*/, const unsigned char** out, unsigned char* outlen, const unsigned char* in,
                               unsigned int inlen, void* /*arg*/)
    {
        static constexpr std::array<unsigned char, 8u> kmx_alpn_wire = {7u, 'k', 'm', 'x', '-', 'a', 'i', 'o'};
        const int selected = ::SSL_select_next_proto(reinterpret_cast<unsigned char**>(const_cast<unsigned char**>(out)), outlen, in, inlen,
                                                     kmx_alpn_wire.data(), static_cast<unsigned int>(kmx_alpn_wire.size()));
        return selected == OPENSSL_NPN_NEGOTIATED ? SSL_TLSEXT_ERR_OK : SSL_TLSEXT_ERR_ALERT_FATAL;
    }

    task<void> handle_stream(::lsquic_stream_t* stream, kmx::aio::quic::stream_payload payload)
    {
        auto data = payload.bytes();
        std::string_view msg(data.data(), data.size());
        std::cout << "Received QUIC stream data: " << msg << "\n";

        const std::string preview(msg.substr(0, std::min<std::size_t>(40u, msg.size())));
        const std::string response = std::format("stream_id={} len={} preview='{}'\n",
                                                 static_cast<unsigned long long>(::lsquic_stream_id(stream)), msg.size(), preview);

        std::size_t written {};
        while (written < response.size())
        {
            const ssize_t chunk = ::lsquic_stream_write(stream, response.data() + written, response.size() - written);
            if (chunk <= 0)
            {
                std::cerr << "Failed to write QUIC echo response on stream " << static_cast<unsigned long long>(::lsquic_stream_id(stream))
                          << "\n";
                co_return;
            }

            written += static_cast<std::size_t>(chunk);
        }

        ::lsquic_stream_flush(stream);
        ::lsquic_stream_shutdown(stream, 1);
        co_return;
    }

    task<void> async_main(executor& exec)
    {
        ::SSL_CTX* ssl_ctx = ::SSL_CTX_new(TLS_server_method());
        if (!ssl_ctx)
        {
            std::cerr << "Failed to create server SSL_CTX\n";
            co_return;
        }
        ::SSL_CTX_set_alpn_select_cb(ssl_ctx, select_kmx_alpn, nullptr);
        if (::SSL_CTX_use_certificate_chain_file(ssl_ctx, "/tmp/quic_cert.pem") != 1)
        {
            std::cerr << "Failed to load /tmp/quic_cert.pem\n";
            ::SSL_CTX_free(ssl_ctx);
            co_return;
        }
        if (::SSL_CTX_use_PrivateKey_file(ssl_ctx, "/tmp/quic_key.pem", SSL_FILETYPE_PEM) != 1)
        {
            std::cerr << "Failed to load /tmp/quic_key.pem\n";
            ::SSL_CTX_free(ssl_ctx);
            co_return;
        }

        kmx::aio::completion::quic::engine engine(exec);
        engine.set_stream_handler(handle_stream);

        static constexpr std::array<std::uint8_t, 4u> ip {127u, 0u, 0u, 1u};
        auto res = co_await engine.start(ip, 12345u, ssl_ctx);
        if (!res)
        {
            std::cerr << "Failed to start QUIC engine: " << res.error().message() << "\n";
            ::SSL_CTX_free(ssl_ctx);
            co_return;
        }

        std::cout << "QUIC server listening on 127.0.0.1:12345\n";

        auto process_res = co_await engine.process();
        if (!process_res)
            std::cerr << "Engine process error: " << process_res.error().message() << "\n";

        ::SSL_CTX_free(ssl_ctx);
    }
}
