#include <kmx/aio/sample/quic/http3_client/manager.hpp>

#include <array>
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

namespace kmx::aio::sample::quic::http3_client
{
    using namespace kmx::aio;
    using namespace kmx::aio::completion;

    namespace detail
    {
        std::uint16_t parse_peer_port_from_env()
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

    task<void> handle_stream(::lsquic_stream_t* stream, kmx::aio::quic::stream_payload payload)
    {
        auto data = payload.bytes();
        const auto raw = std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());

        const auto control_state = kmx::aio::http3::control_stream_codec::decode(raw);
        if (control_state)
        {
            if (control_state->saw_settings)
            {
                std::cout << "[HTTP/3 Client] Received peer control stream SETTINGS"
                          << " max_field_section_size=" << control_state->negotiated_settings.max_field_section_size
                          << " qpack_blocked_streams=" << control_state->negotiated_settings.qpack_blocked_streams << "\n";
            }

            if (control_state->goaway.has_value())
                std::cout << "[HTTP/3 Client] Received peer GOAWAY stream_id=" << control_state->goaway->stream_id << "\n";

            co_return;
        }

        const auto frames = kmx::aio::http3::frame_codec::decode_all(raw);
        std::cout << "\n[HTTP/3 Client] Received Server Response:\n"
                  << "--------------------------------------------------------\n"
                  << "binary payload bytes=" << data.size() << "\n"
                  << "--------------------------------------------------------\n";

        if (frames)
        {
            kmx::aio::http3::response_message response {};
            for (const auto& frame: *frames)
            {
                if (frame.type == kmx::aio::http3::frame_type::headers)
                {
                    const auto headers = kmx::aio::http3::headers_codec::decode(frame.payload);
                    if (!headers)
                        continue;

                    for (const auto& [name, value]: *headers)
                    {
                        if (name == ":status")
                            response.head.status = static_cast<std::uint16_t>(std::stoi(value));
                        else
                            response.head.headers.emplace_back(name, value);
                    }
                }
                else if (frame.type == kmx::aio::http3::frame_type::data)
                {
                    const auto body = kmx::aio::http3::data_codec::decode(frame.payload);
                    if (body)
                        response.body.append(reinterpret_cast<const char*>(body->data()), body->size());
                }
            }

            std::cout << "[HTTP/3 Client] Parsed status: " << response.head.status << "\n";
            std::cout << "[HTTP/3 Client] Parsed body bytes: " << response.body.size() << "\n";
            std::cout << response.body << "\n";
        }

        ::lsquic_conn_close(::lsquic_stream_conn(stream));
        co_return; // No further action needed.
    }

    task<void> async_main(executor& exec)
    {
        // Client SSL setup
        ::SSL_CTX* ssl_ctx = ::SSL_CTX_new(TLS_client_method());
        if (!ssl_ctx)
        {
            std::cerr << "Failed to create client SSL_CTX\n";
            co_return;
        }
        if (::SSL_CTX_set_alpn_protos(ssl_ctx, kmx::aio::http3::alpn::wire.data(),
                                      static_cast<unsigned int>(kmx::aio::http3::alpn::wire.size())) != 0)
        {
            std::cerr << "Failed to configure client ALPN\n";
            ::SSL_CTX_free(ssl_ctx);
            co_return;
        }
        // For testing purposes, we might not verify the cert directly.
        ::SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, nullptr);

        kmx::aio::completion::quic::engine engine(exec);
        // stream handler is used when server writes back to the stream we created
        engine.set_stream_handler(handle_stream);
        engine.set_alpn(std::string(kmx::aio::http3::alpn::id));

        // Dial the server
        kmx::aio::http3::header_list request_headers {
            {":method", "GET"},
            {":scheme", "https"},
            {":authority", "localhost"},
            {":path", "/"},
        };
        const auto encoded_frames = kmx::aio::http3::headers_codec::encode_frame(request_headers);
        std::string payload(reinterpret_cast<const char*>(encoded_frames.data()), encoded_frames.size());
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
