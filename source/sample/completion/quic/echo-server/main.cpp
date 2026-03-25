#include <iostream>
#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/quic/engine.hpp>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <memory>

using namespace kmx::aio;
using namespace kmx::aio::completion;

task<void> handle_stream(std::span<char> data)
{
    std::string_view msg(data.data(), data.size());
    std::cout << "Received QUIC stream data: " << msg << "\n";
    co_return;
}

task<void> async_main(std::shared_ptr<executor> exec)
{
    ::SSL_CTX* ssl_ctx = ::SSL_CTX_new(TLS_server_method());
    ::SSL_CTX_use_certificate_chain_file(ssl_ctx, "/tmp/quic_cert.pem");
    ::SSL_CTX_use_PrivateKey_file(ssl_ctx, "/tmp/quic_key.pem", SSL_FILETYPE_PEM);

    kmx::aio::completion::quic::engine engine(*exec);
    engine.set_stream_handler(handle_stream);

    static constexpr std::array<unsigned char, 4> ip = {127, 0, 0, 1};
    auto res = co_await engine.start(ip, 12345, ssl_ctx);
    if (!res)
    {
        std::cerr << "Failed to start QUIC engine: " << res.error().message() << "\n";
        co_return;
    }

    std::cout << "QUIC server listening on 127.0.0.1:12345\n";

    auto process_res = co_await engine.process();
    if (!process_res)
    {
        std::cerr << "Engine process error: " << process_res.error().message() << "\n";
    }
    ::SSL_CTX_free(ssl_ctx);
}

int main()
{
    try
    {
        auto exec = std::make_shared<executor>();
        exec->spawn(async_main(exec));
        exec->run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
