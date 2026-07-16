/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/modbus/tls_server.hpp>
#if defined(KMX_AIO_FEATURE_MODBUS)
    #include <kmx/aio/modbus/detail/server_ops.hpp>
    #include <kmx/aio/modbus/detail/session.hpp>
    #include <kmx/aio/modbus/error.hpp>
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
    // Type alias for common async result type
    using async_result = task<std::expected<void, std::error_code>>;

    struct tls_server::impl : detail::server_ops<tls_server::impl>
    {
        std::unordered_map<std::uint8_t, request_handler> handlers_;
        std::stop_source stop_source_;

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
                ::SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                                    nullptr);
            else
                ::SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

            return ctx;
        }

        [[nodiscard]] task<void>
        handle_connection(readiness::executor& exec, file_descriptor fd,
                          const server_config& config, ::SSL_CTX* ssl_ctx) noexcept(false)
        {
            // Wrap in TLS stream and handshake
            readiness::tcp::stream tcp_stream {exec, std::move(fd)};
            readiness::tls::stream tls_stream {std::move(tcp_stream), ssl_ctx};
            tls_stream.set_accept_state();

            if (const auto r = co_await tls_stream.handshake(); !r)
                co_return;

            const auto stop_token = stop_source_.get_token();

            while (!stop_token.stop_requested())
                co_await process_request(tls_stream, config);
        }
    };

    tls_server::tls_server() noexcept: impl_(std::make_unique<impl>()) {}

    tls_server::~tls_server() noexcept = default;
    tls_server::tls_server(tls_server&&) noexcept = default;
    tls_server& tls_server::operator=(tls_server&&) noexcept = default;

    void tls_server::set_handler(const function_code fc, request_handler handler)
    {
        impl_->handlers_[static_cast<std::uint8_t>(fc)] = std::move(handler);
    }

    async_result
    tls_server::serve(readiness::executor& exec, server_config config,
                      tls_config tls) noexcept(false)
    {
        const auto ctx_result = impl::create_ssl_ctx(tls);
        if (!ctx_result)
            co_return std::unexpected(ctx_result.error());

        ::SSL_CTX* ssl_ctx = *ctx_result;

        ipv4_storage_t bind_ip = any_ipv4;
        if (!config.bind_address.empty())
        {
            bind_ip = ipv4_storage_t {};
            if (!parse_ipv4_address(config.bind_address, bind_ip))
            {
                ::SSL_CTX_free(ssl_ctx);
                co_return std::unexpected(make_error_code(error::invalid_configuration));
            }
        }

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
        co_return std::expected<void, std::error_code>();
    }

    void tls_server::stop() noexcept
    {
        impl_->stop_source_.request_stop();
    }

} // namespace kmx::aio::modbus
#endif // KMX_AIO_FEATURE_MODBUS
