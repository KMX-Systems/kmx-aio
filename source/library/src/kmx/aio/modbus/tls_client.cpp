/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/modbus/tls_client.hpp>
#if defined(KMX_AIO_FEATURE_MODBUS)
    #include <kmx/aio/modbus/detail/session.hpp>
    #include <kmx/aio/modbus/frame.hpp>
    #include <kmx/aio/readiness/basic_types.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/readiness/tcp/stream.hpp>
    #include <kmx/aio/readiness/tls/stream.hpp>

    #include <netinet/tcp.h>
    #include <openssl/ssl.h>
    #include <sys/socket.h>

    #include <cstdint>
    #include <optional>
    #include <utility>
    #include <vector>

namespace kmx::aio::modbus
{
    // =========================================================================
    // pimpl
    // =========================================================================

    struct tls_client::impl
    {
        readiness::executor& exec_;
        client_config config_;
        tls_config tls_config_;
        std::optional<readiness::tls::stream> stream_;
        ::SSL_CTX* ssl_ctx_ = nullptr;
        std::uint16_t next_tid_ = 0u;

        explicit impl(client_config config, tls_config tls, readiness::executor& exec) noexcept
            : exec_(exec)
            , config_(std::move(config))
            , tls_config_(std::move(tls))
        {
        }

        ~impl() noexcept
        {
            if (ssl_ctx_)
            {
                ::SSL_CTX_free(ssl_ctx_);
                ssl_ctx_ = nullptr;
            }
        }

        // -----------------------------------------------------------------
        // SSL_CTX setup
        // -----------------------------------------------------------------

        [[nodiscard]] std::expected<::SSL_CTX*, std::error_code> create_ssl_ctx() noexcept
        {
            ::SSL_CTX* ctx = ::SSL_CTX_new(::TLS_client_method());
            if (!ctx)
                return std::unexpected(make_error_code(error::tls_handshake_failed));

            // Require TLS 1.2 minimum (Modbus Security Specification §3.3)
            ::SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

            // Load client certificate and key for mTLS
            if (!tls_config_.cert_path.empty())
            {
                if (::SSL_CTX_use_certificate_file(ctx, tls_config_.cert_path.c_str(),
                                                   SSL_FILETYPE_PEM) != 1)
                {
                    ::SSL_CTX_free(ctx);
                    return std::unexpected(make_error_code(error::tls_handshake_failed));
                }
                if (::SSL_CTX_use_PrivateKey_file(ctx, tls_config_.key_path.c_str(),
                                                  SSL_FILETYPE_PEM) != 1)
                {
                    ::SSL_CTX_free(ctx);
                    return std::unexpected(make_error_code(error::tls_handshake_failed));
                }
            }

            // Load CA for server certificate verification
            if (!tls_config_.ca_cert_path.empty())
            {
                if (::SSL_CTX_load_verify_locations(ctx, tls_config_.ca_cert_path.c_str(),
                                                    nullptr) != 1)
                {
                    ::SSL_CTX_free(ctx);
                    return std::unexpected(make_error_code(error::tls_handshake_failed));
                }
            }

            // Peer verification (verify server cert)
            if (tls_config_.verify_peer)
            {
                ::SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
            }
            else
            {
                ::SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
            }

            return ctx;
        }

        // -----------------------------------------------------------------
        // Connection management
        // -----------------------------------------------------------------

        [[nodiscard]] task<std::expected<void, std::error_code>> connect() noexcept(false)
        {
            if (stream_.has_value())
                co_return {};

            // Build SSL_CTX
            if (!ssl_ctx_)
            {
                const auto ctx_result = create_ssl_ctx();
                if (!ctx_result)
                    co_return std::unexpected(ctx_result.error());
                ssl_ctx_ = *ctx_result;
            }

            // Parse IPv4 host
            ipv4_storage_t ip_storage {};
            if (!parse_ipv4_address(config_.host, ip_storage))
                co_return std::unexpected(make_error_code(error::invalid_configuration));

            const auto ip = make_ipv4_address(ip_storage);

            // Create non-blocking TCP socket
            auto fd_result = file_descriptor::create_socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
            if (!fd_result)
                co_return std::unexpected(make_error_code(error::connection_failed));

            auto fd = std::move(*fd_result);

            const int one = 1;
            ::setsockopt(fd.get(), IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

            if (auto r = exec_.register_fd(fd.get()); !r)
                co_return std::unexpected(make_error_code(error::connection_failed));

            const auto connect_result = fd.connect(ip, config_.port);
            const bool in_progress =
                !connect_result &&
                connect_result.error() == std::error_code(EINPROGRESS, std::generic_category());

            if (!connect_result && !in_progress)
            {
                exec_.unregister_fd(fd.get());
                co_return std::unexpected(make_error_code(error::connection_failed));
            }

            if (in_progress)
            {
                if (auto r = co_await exec_.wait_io(fd.get(), readiness::event_type::write); !r)
                {
                    exec_.unregister_fd(fd.get());
                    co_return std::unexpected(make_error_code(error::connection_failed));
                }
            }

            int so_error = 0;
            ::socklen_t so_len = sizeof(so_error);
            if (::getsockopt(fd.get(), SOL_SOCKET, SO_ERROR, &so_error, &so_len) != 0 ||
                so_error != 0)
            {
                exec_.unregister_fd(fd.get());
                co_return std::unexpected(make_error_code(error::connection_failed));
            }

            // Wrap TCP stream in TLS stream and perform handshake
            readiness::tcp::stream tcp_stream {exec_, std::move(fd)};
            readiness::tls::stream tls_stream {std::move(tcp_stream), ssl_ctx_};

            if (!tls_config_.sni_hostname.empty())
            {
                ::SSL_set_tlsext_host_name(tls_stream.native_handle(),
                                           tls_config_.sni_hostname.c_str());
            }

            tls_stream.set_connect_state();

            if (auto r = co_await tls_stream.handshake(); !r)
                co_return std::unexpected(make_error_code(error::tls_handshake_failed));

            stream_.emplace(std::move(tls_stream));
            co_return {};
        }

        [[nodiscard]] task<std::expected<void, std::error_code>> disconnect() noexcept(false)
        {
            stream_.reset();
            co_return {};
        }

        // -----------------------------------------------------------------
        // Internal exchange helper (mirrors client.cpp)
        // -----------------------------------------------------------------

        [[nodiscard]] task<std::expected<std::vector<std::uint8_t>, std::error_code>>
        exchange_pdu(const std::span<const std::uint8_t> pdu_bytes) noexcept(false)
        {
            if (!stream_.has_value())
                co_return std::unexpected(make_error_code(error::disconnected));

            const auto tid = next_tid_++;
            const auto pdu_len = static_cast<std::uint16_t>(pdu_bytes.size());

            std::vector<std::uint8_t> adu(frame::mbap_size + pdu_len);
            frame::encode_mbap(adu, tid, pdu_len, config_.unit_id);
            std::ranges::copy(pdu_bytes,
                              adu.begin() + static_cast<std::ptrdiff_t>(frame::mbap_size));

            co_return co_await detail::exchange(*stream_, adu, tid, config_.unit_id);
        }

        [[nodiscard]] task<std::expected<register_values, std::error_code>>
        read_registers(const function_code fc, const std::uint16_t address,
                       const std::uint16_t count) noexcept(false)
        {
            const auto pdu_result = frame::encode_read_request(fc, address, count);
            if (!pdu_result)
                co_return std::unexpected(pdu_result.error());

            auto response = co_await exchange_pdu(*pdu_result);
            if (!response)
                co_return std::unexpected(response.error());

            co_return frame::decode_read_registers_response(*response, fc, count);
        }

        [[nodiscard]] task<std::expected<coil_values, std::error_code>>
        read_coils_impl(const function_code fc, const std::uint16_t address,
                        const std::uint16_t count) noexcept(false)
        {
            const auto pdu_result = frame::encode_read_request(fc, address, count);
            if (!pdu_result)
                co_return std::unexpected(pdu_result.error());

            auto response = co_await exchange_pdu(*pdu_result);
            if (!response)
                co_return std::unexpected(response.error());

            co_return frame::decode_read_coils_response(*response, fc, count);
        }
    };

    // =========================================================================
    // tls_client public API
    // =========================================================================

    tls_client::tls_client(client_config config, tls_config tls, readiness::executor& exec) noexcept
        : impl_(std::make_unique<impl>(std::move(config), std::move(tls), exec))
    {
    }

    tls_client::~tls_client() noexcept = default;
    tls_client::tls_client(tls_client&&) noexcept = default;
    tls_client& tls_client::operator=(tls_client&&) noexcept = default;

    task<std::expected<void, std::error_code>> tls_client::connect() noexcept(false)
    {
        return impl_->connect();
    }

    task<std::expected<void, std::error_code>> tls_client::disconnect() noexcept(false)
    {
        return impl_->disconnect();
    }

    task<std::expected<register_values, std::error_code>>
    tls_client::read_holding_registers(const std::uint16_t address, const std::uint16_t count) noexcept(false)
    {
        return impl_->read_registers(function_code::read_holding_registers, address, count);
    }

    task<std::expected<register_values, std::error_code>>
    tls_client::read_input_registers(const std::uint16_t address, const std::uint16_t count) noexcept(false)
    {
        return impl_->read_registers(function_code::read_input_registers, address, count);
    }

    task<std::expected<coil_values, std::error_code>>
    tls_client::read_coils(const std::uint16_t address, const std::uint16_t count) noexcept(false)
    {
        return impl_->read_coils_impl(function_code::read_coils, address, count);
    }

    task<std::expected<coil_values, std::error_code>>
    tls_client::read_discrete_inputs(const std::uint16_t address, const std::uint16_t count) noexcept(false)
    {
        return impl_->read_coils_impl(function_code::read_discrete_inputs, address, count);
    }

    task<std::expected<void, std::error_code>>
    tls_client::write_single_register(const std::uint16_t address, const std::uint16_t value) noexcept(false)
    {
        const auto pdu = frame::encode_write_single_register(address, value);
        auto self = impl_.get();
        auto response = co_await self->exchange_pdu(pdu);
        if (!response)
            co_return std::unexpected(response.error());
        co_return frame::decode_write_single_response(*response, function_code::write_single_register);
    }

    task<std::expected<void, std::error_code>>
    tls_client::write_single_coil(const std::uint16_t address, const bool on) noexcept(false)
    {
        const auto pdu = frame::encode_write_single_coil(address, on);
        auto self = impl_.get();
        auto response = co_await self->exchange_pdu(pdu);
        if (!response)
            co_return std::unexpected(response.error());
        co_return frame::decode_write_single_response(*response, function_code::write_single_coil);
    }

    task<std::expected<void, std::error_code>>
    tls_client::write_multiple_registers(const std::uint16_t address,
                                         const std::span<const std::uint16_t> values) noexcept(false)
    {
        auto self = impl_.get();
        const auto pdu_result = frame::encode_write_multiple_registers(address, values);
        if (!pdu_result)
            co_return std::unexpected(pdu_result.error());

        auto response = co_await self->exchange_pdu(*pdu_result);
        if (!response)
            co_return std::unexpected(response.error());
        co_return frame::decode_write_multiple_response(*response, function_code::write_multiple_registers);
    }

    task<std::expected<void, std::error_code>>
    tls_client::write_multiple_coils(const std::uint16_t address,
                                     const std::span<const std::uint8_t> values) noexcept(false)
    {
        auto self = impl_.get();
        const auto pdu_result = frame::encode_write_multiple_coils(address, values);
        if (!pdu_result)
            co_return std::unexpected(pdu_result.error());

        auto response = co_await self->exchange_pdu(*pdu_result);
        if (!response)
            co_return std::unexpected(response.error());
        co_return frame::decode_write_multiple_response(*response, function_code::write_multiple_coils);
    }

    bool tls_client::is_connected() const noexcept
    {
        return impl_->stream_.has_value();
    }

} // namespace kmx::aio::modbus
#endif // KMX_AIO_FEATURE_MODBUS
