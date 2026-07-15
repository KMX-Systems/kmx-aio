/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/modbus/tls_server.hpp>
#if defined(KMX_AIO_FEATURE_MODBUS)
    #include <kmx/aio/modbus/detail/session.hpp>
    #include <kmx/aio/modbus/frame.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/readiness/tcp/listener.hpp>
    #include <kmx/aio/readiness/tcp/stream.hpp>
    #include <kmx/aio/readiness/tls/stream.hpp>

    #include <openssl/ssl.h>

    #include <stop_token>
    #include <unordered_map>
    #include <utility>

namespace kmx::aio::modbus
{
    namespace
    {
        [[nodiscard]] std::vector<std::uint8_t>
        make_exception_response_tls(const std::uint8_t request_fc, const exception_code ec) noexcept
        {
            return {static_cast<std::uint8_t>(request_fc | frame::exception_fc_flag),
                    static_cast<std::uint8_t>(ec)};
        }
    }

    // =========================================================================
    // pimpl
    // =========================================================================

    struct tls_server::impl
    {
        std::unordered_map<std::uint8_t, request_handler> handlers_;
        std::stop_source stop_source_;

        // -----------------------------------------------------------------
        // SSL_CTX setup
        // -----------------------------------------------------------------

        [[nodiscard]] static std::expected<::SSL_CTX*, std::error_code>
        create_ssl_ctx(const tls_config& tls) noexcept
        {
            ::SSL_CTX* ctx = ::SSL_CTX_new(::TLS_server_method());
            if (!ctx)
                return std::unexpected(make_error_code(error::tls_handshake_failed));

            ::SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

            if (!tls.cert_path.empty())
            {
                if (::SSL_CTX_use_certificate_file(ctx, tls.cert_path.c_str(),
                                                   SSL_FILETYPE_PEM) != 1)
                {
                    ::SSL_CTX_free(ctx);
                    return std::unexpected(make_error_code(error::tls_handshake_failed));
                }
                if (::SSL_CTX_use_PrivateKey_file(ctx, tls.key_path.c_str(),
                                                  SSL_FILETYPE_PEM) != 1)
                {
                    ::SSL_CTX_free(ctx);
                    return std::unexpected(make_error_code(error::tls_handshake_failed));
                }
            }

            if (!tls.ca_cert_path.empty())
            {
                if (::SSL_CTX_load_verify_locations(ctx, tls.ca_cert_path.c_str(),
                                                    nullptr) != 1)
                {
                    ::SSL_CTX_free(ctx);
                    return std::unexpected(make_error_code(error::tls_handshake_failed));
                }
                // Use the same CA as trusted client CA list
                ::SSL_CTX_set_client_CA_list(
                    ctx, ::SSL_load_client_CA_file(tls.ca_cert_path.c_str()));
            }

            if (tls.verify_peer)
            {
                ::SSL_CTX_set_verify(
                    ctx,
                    SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                    nullptr);
            }
            else
            {
                ::SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
            }

            return ctx;
        }

        // -----------------------------------------------------------------
        // Per-connection coroutine
        // -----------------------------------------------------------------

        task<void> handle_connection(readiness::executor& exec, file_descriptor fd,
                                     const server_config& config,
                                     ::SSL_CTX* ssl_ctx) noexcept(false)
        {
            // Wrap in TLS stream and handshake
            readiness::tcp::stream tcp_stream {exec, std::move(fd)};
            readiness::tls::stream tls_stream {std::move(tcp_stream), ssl_ctx};
            tls_stream.set_accept_state();

            if (const auto r = co_await tls_stream.handshake(); !r)
                co_return;

            const auto stop_token = stop_source_.get_token();

            while (!stop_token.stop_requested())
            {
                std::array<std::uint8_t, frame::mbap_size> hdr_buf {};
                {
                    auto span = std::span<char>(
                        reinterpret_cast<char*>(hdr_buf.data()), hdr_buf.size()); // NOLINT
                    if (const auto r = co_await detail::read_exactly(tls_stream, span); !r)
                        co_return;
                }

                const auto hdr = frame::decode_mbap(hdr_buf);
                if (!hdr)
                    co_return;

                if (config.unit_id != broadcast_unit_id && hdr->unit_id != config.unit_id)
                    co_return;

                if (hdr->length < 1u)
                    co_return;

                const std::size_t pdu_len = static_cast<std::size_t>(hdr->length) - 1u;
                std::vector<std::uint8_t> pdu(pdu_len);

                if (pdu_len > 0u)
                {
                    auto span = std::span<char>(
                        reinterpret_cast<char*>(pdu.data()), pdu.size()); // NOLINT
                    if (const auto r = co_await detail::read_exactly(tls_stream, span); !r)
                        co_return;
                }

                if (pdu.empty())
                    co_return;

                const std::uint8_t request_fc = pdu[0];

                std::vector<std::uint8_t> response_pdu;
                if (const auto it = handlers_.find(request_fc); it != handlers_.end())
                {
                    server_request req {.unit_id = hdr->unit_id, .pdu = std::move(pdu)};
                    response_pdu = co_await it->second(std::move(req));
                }
                else
                {
                    response_pdu = make_exception_response_tls(request_fc,
                                                               exception_code::illegal_function);
                }

                const auto resp_pdu_len = static_cast<std::uint16_t>(response_pdu.size());
                std::vector<std::uint8_t> response_adu(frame::mbap_size + resp_pdu_len);
                frame::encode_mbap(response_adu, hdr->transaction_id, resp_pdu_len, hdr->unit_id);
                std::ranges::copy(response_pdu,
                                  response_adu.begin() +
                                      static_cast<std::ptrdiff_t>(frame::mbap_size));

                const auto send_view = std::span<const char>(
                    reinterpret_cast<const char*>(response_adu.data()), // NOLINT
                    response_adu.size());
                if (const auto r = co_await tls_stream.write_all(send_view); !r)
                    co_return;
            }
        }
    };

    // =========================================================================
    // tls_server public API
    // =========================================================================

    tls_server::tls_server() noexcept: impl_(std::make_unique<impl>()) {}

    tls_server::~tls_server() noexcept = default;
    tls_server::tls_server(tls_server&&) noexcept = default;
    tls_server& tls_server::operator=(tls_server&&) noexcept = default;

    void tls_server::set_handler(const function_code fc, request_handler handler)
    {
        impl_->handlers_[static_cast<std::uint8_t>(fc)] = std::move(handler);
    }

    task<std::expected<void, std::error_code>>
    tls_server::serve(readiness::executor& exec, server_config config,
                      tls_config tls) noexcept(false)
    {
        const auto ctx_result = impl::create_ssl_ctx(tls);
        if (!ctx_result)
            co_return std::unexpected(ctx_result.error());

        ::SSL_CTX* ssl_ctx = *ctx_result;

        const ipv4_storage_t bind_ip = config.bind_address.empty()
                                           ? any_ipv4
                                           : [&]() -> ipv4_storage_t {
            ipv4_storage_t ip {};
            parse_ipv4_address(config.bind_address, ip);
            return ip;
        }();

        readiness::tcp::listener listener {exec, make_ipv4_address(bind_ip), config.port};
        if (const auto r = listener.listen(); !r)
        {
            ::SSL_CTX_free(ssl_ctx);
            co_return std::unexpected(r.error());
        }

        const auto stop_token = impl_->stop_source_.get_token();

        while (!stop_token.stop_requested())
        {
            auto fd_result = co_await listener.accept();
            if (!fd_result)
            {
                if (stop_token.stop_requested())
                    break;
                ::SSL_CTX_free(ssl_ctx);
                co_return std::unexpected(fd_result.error());
            }

            exec.spawn(
                impl_->handle_connection(exec, std::move(*fd_result), config, ssl_ctx));
        }

        ::SSL_CTX_free(ssl_ctx);
        co_return {};
    }

    void tls_server::stop() noexcept
    {
        impl_->stop_source_.request_stop();
    }

} // namespace kmx::aio::modbus
#endif // KMX_AIO_FEATURE_MODBUS
