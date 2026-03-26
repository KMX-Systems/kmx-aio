#include <kmx/aio/sample/quic/echo_server/manager.hpp>

#include <iostream>
#include <kmx/aio/completion/quic/engine.hpp>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <vector>
#include <string_view>
#include <cstdint>
#include <array>

namespace kmx::aio::sample::quic::echo_server
{
    using namespace kmx::aio;
    using namespace kmx::aio::completion;

    task<void> handle_stream(std::span<char> data)
    {
        std::string_view msg(data.data(), data.size());
        std::cout << "Received QUIC stream data: " << msg << "\n";
        co_return;
    }

    auto async_main(std::shared_ptr<executor> exec) -> task<void>
    {
        ::SSL_CTX* ssl_ctx = ::SSL_CTX_new(TLS_server_method());
        ::SSL_CTX_use_certificate_chain_file(ssl_ctx, "/tmp/quic_cert.pem");
        ::SSL_CTX_use_PrivateKey_file(ssl_ctx, "/tmp/quic_key.pem", SSL_FILETYPE_PEM);

        kmx::aio::completion::quic::engine engine(*exec);
        engine.set_stream_handler(handle_stream);

        static constexpr std::array<std::uint8_t, 4u> ip{127u, 0u, 0u, 1u};
        auto res = co_await engine.start(ip, 12345u, ssl_ctx);
        if (!res)
        {
            std::cerr << "Failed to start QUIC engine: " << res.error().message() << "\n";
            co_return;
        }

        std::cout << "QUIC server listening on 127.0.0.1:12345\n";

        auto process_res = co_await engine.process();
        if (!process_res)
            std::cerr << "Engine process error: " << process_res.error().message() << "\n";

        ::SSL_CTX_free(ssl_ctx);
    }
}
