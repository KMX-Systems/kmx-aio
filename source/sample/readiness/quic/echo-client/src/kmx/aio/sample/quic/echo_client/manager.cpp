#include <kmx/aio/sample/quic/echo_client/manager.hpp>

#include <array>
#include <atomic>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <kmx/aio/readiness/quic/engine.hpp>
#include <lsquic.h>
#include <openssl/ssl.h>
#include <string>
#include <string_view>
#include <vector>

namespace kmx::aio::sample::quic::echo_client
{
    using namespace kmx::aio;
    using namespace kmx::aio::readiness;

    namespace detail
    {
        std::atomic_uint32_t responses_received {};
        std::atomic_uint32_t close_after_responses {2u};

        std::uint16_t parse_peer_port_from_env()
        {
            constexpr std::uint16_t default_port = 12345u;
            const char* const env = std::getenv("KMX_QUIC_ECHO_PORT");
            if (!env || (env[0] == '\0'))
                return default_port;

            std::uint32_t parsed {};
            const char* const end = env + std::char_traits<char>::length(env);
            const auto [ptr, ec] = std::from_chars(env, end, parsed);
            if ((ec != std::errc()) || (ptr != end) || (parsed == 0u) || (parsed > 65535u))
                return default_port;

            return static_cast<std::uint16_t>(parsed);
        }

        std::uint32_t parse_response_target_from_env()
        {
            constexpr std::uint32_t default_target = 2u;
            const char* const env = std::getenv("KMX_QUIC_ECHO_CLIENT_CLOSE_AFTER_RESPONSES");
            if (!env || (env[0] == '\0'))
                return default_target;

            std::uint32_t parsed {};
            const char* const end = env + std::char_traits<char>::length(env);
            const auto [ptr, ec] = std::from_chars(env, end, parsed);
            if ((ec != std::errc()) || (ptr != end))
                return default_target;

            return parsed;
        }
    }

    task<void> handle_stream(::lsquic_stream_t* stream, kmx::aio::quic::stream_payload payload)
    {
        auto data = payload.bytes();
        std::string_view response(data.data(), data.size());
        const auto seen = detail::responses_received.fetch_add(1u) + 1u;

        std::cout << "[QUIC Readiness Echo Client] Response #" << seen << " on stream "
                  << static_cast<unsigned long long>(::lsquic_stream_id(stream)) << ": " << response << "\n";

        const auto target = detail::close_after_responses.load();
        if (target > 0u && seen >= target)
            ::lsquic_conn_close(::lsquic_stream_conn(stream));

        co_return;
    }

    task<void> async_main(std::shared_ptr<executor> exec)
    {
        detail::responses_received.store(0u);
        detail::close_after_responses.store(detail::parse_response_target_from_env());

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

        ::SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, nullptr);

        kmx::aio::readiness::quic::engine engine(*exec);
        engine.set_stream_handler(handle_stream);

        static constexpr std::array<unsigned char, 4u> peer_ip = {127u, 0u, 0u, 1u};
        const std::uint16_t peer_port = detail::parse_peer_port_from_env();

        const std::vector<std::string> payloads = {
            "echo stream A payload",
            "echo stream B payload",
        };

        std::cout << "[QUIC Readiness Echo Client] Connecting to 127.0.0.1:" << peer_port << " with " << payloads.size() << " streams...\n";
        std::cout << "[QUIC Readiness Echo Client] close_after_responses=" << detail::close_after_responses.load() << "\n";

        auto res = co_await engine.connect(peer_ip, peer_port, "localhost", payloads, ssl_ctx);
        if (!res)
        {
            std::cerr << "Failed to connect engine: " << res.error().message() << "\n";
            ::SSL_CTX_free(ssl_ctx);
            co_return;
        }

        auto process_res = co_await engine.process();
        if (!process_res)
            std::cerr << "Engine process error: " << process_res.error().message() << "\n";

        ::SSL_CTX_free(ssl_ctx);
    }
}
