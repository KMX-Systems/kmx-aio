#include <kmx/aio/sample/quic/http3_server/manager.hpp>

#include <array>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <kmx/aio/completion/quic/engine.hpp>
#include <kmx/aio/http3/alpn.hpp>
#include <kmx/aio/http3/codec.hpp>
#include <kmx/aio/http3/message.hpp>
#include <lsquic.h>
#include <memory>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string>
#include <string_view>

namespace kmx::aio::sample::quic::http3_server
{
    using namespace kmx::aio;
    using namespace kmx::aio::completion;

    namespace detail
    {
        std::uint16_t parse_listen_port_from_env()
        {
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

    static int select_http3_alpn(::SSL* /*ssl*/, const unsigned char** out, unsigned char* outlen, const unsigned char* in,
                                 unsigned int inlen, void* /*arg*/)
    {
        const int selected =
            ::SSL_select_next_proto(reinterpret_cast<unsigned char**>(const_cast<unsigned char**>(out)), outlen, in, inlen,
                                    kmx::aio::http3::alpn::wire.data(), static_cast<unsigned int>(kmx::aio::http3::alpn::wire.size()));
        return selected == OPENSSL_NPN_NEGOTIATED ? SSL_TLSEXT_ERR_OK : SSL_TLSEXT_ERR_ALERT_FATAL;
    }

    task<void> handle_stream(::lsquic_stream_t* stream, kmx::aio::quic::stream_payload payload)
    {
        auto data = payload.bytes();
        const auto raw = std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());

        const auto control_state = kmx::aio::http3::control_stream_codec::decode(raw);
        if (control_state)
        {
            if (control_state->saw_settings)
            {
                std::cout << "[HTTP/3 Server] Received peer control stream SETTINGS"
                          << " max_field_section_size=" << control_state->negotiated_settings.max_field_section_size
                          << " qpack_blocked_streams=" << control_state->negotiated_settings.qpack_blocked_streams << "\n";
            }

            if (control_state->goaway.has_value())
                std::cout << "[HTTP/3 Server] Received peer GOAWAY stream_id=" << control_state->goaway->stream_id << "\n";

            co_return;
        }

        const auto frames = kmx::aio::http3::frame_codec::decode_all(raw);
        if (frames)
        {
            kmx::aio::http3::request_message request {};
            for (const auto& frame: *frames)
            {
                if (frame.type == kmx::aio::http3::frame_type::headers)
                {
                    const auto headers = kmx::aio::http3::headers_codec::decode(frame.payload);
                    if (!headers)
                        continue;

                    for (const auto& [name, value]: *headers)
                    {
                        if (name == ":method")
                            request.head.method = value;
                        else if (name == ":path")
                            request.head.target = value;
                        else if (name == ":authority")
                            request.head.authority = value;
                        else if (name == ":scheme")
                            request.head.scheme = value;
                        else
                            request.head.headers.emplace_back(name, value);
                    }
                }
                else if (frame.type == kmx::aio::http3::frame_type::data)
                {
                    const auto body = kmx::aio::http3::data_codec::decode(frame.payload);
                    if (body)
                        request.body.append(reinterpret_cast<const char*>(body->data()), body->size());
                }
            }

            std::cout << "[HTTP/3 Server] Parsed request method=" << request.head.method << " target=" << request.head.target
                      << " authority=" << request.head.authority << "\n";
        }

        kmx::aio::http3::header_list response_headers {
            {":status", "200"},
            {"Content-Type", "text/html"},
        };
        auto response_frames = kmx::aio::http3::headers_codec::encode_frame(response_headers);
        const auto body_frame = kmx::aio::http3::data_codec::encode_frame(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>("<html><body><h1>Hello from KMX AIO QUIC HTTP3 Server!</h1></body></html>\n"),
            sizeof("<html><body><h1>Hello from KMX AIO QUIC HTTP3 Server!</h1></body></html>\n") - 1u));
        response_frames.insert(response_frames.end(), body_frame.begin(), body_frame.end());
        std::string response(reinterpret_cast<const char*>(response_frames.data()), response_frames.size());

        std::size_t written = 0;
        while (written < response.size())
        {
            const ssize_t chunk = ::lsquic_stream_write(stream, response.data() + written, response.size() - written);
            if (chunk <= 0)
            {
                std::cerr << "Failed to write HTTP3 response on QUIC stream. errno=" << errno << "\n";
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
        ::SSL_CTX_set_alpn_select_cb(ssl_ctx, select_http3_alpn, nullptr);
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

        const std::uint16_t listen_port = detail::parse_listen_port_from_env();

        kmx::aio::completion::quic::engine engine(exec);
        engine.set_stream_handler(handle_stream);
        engine.set_alpn(std::string(kmx::aio::http3::alpn::id));

        static constexpr std::array<std::uint8_t, 4u> ip = {127u, 0u, 0u, 1u};
        auto res = co_await engine.start(ip, listen_port, ssl_ctx);
        if (!res)
        {
            std::cerr << "Failed to start QUIC engine: " << res.error().message() << "\n";
            ::SSL_CTX_free(ssl_ctx);
            co_return;
        }

        std::cout << "QUIC (HTTP3) server listening on 127.0.0.1:" << listen_port << "\n";

        auto process_res = co_await engine.process();
        if (!process_res)
        {
            std::cerr << "Engine process error: " << process_res.error().message() << "\n";
        }
        ::SSL_CTX_free(ssl_ctx);
    }
}
