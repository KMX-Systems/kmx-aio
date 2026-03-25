/// @file aio/tls/stream.hpp
/// @brief Generic TLS stream template using BoringSSL Memory BIOs.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <expected>
    #include <new>
    #include <optional>
    #include <span>
    #include <string_view>
    #include <system_error>
    #include <utility>

    #include <openssl/bio.h>
    #include <openssl/err.h>
    #include <openssl/ssl.h>

    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::tls
{
    /// @brief Generic Asynchronous TLS stream wrapping any inner stream interface.
    template <typename InnerStream>
    class stream
    {
    public:
        using result_task = task<std::expected<std::size_t, std::error_code>>;

        stream() noexcept = default;

        /// @brief Constructs a TLS stream from an existing socket and SSL context.
        stream(InnerStream inner, ::SSL_CTX* ctx) noexcept(false): inner_(std::move(inner))
        {
            ssl_ = ::SSL_new(ctx);
            if (!ssl_)
                throw std::bad_alloc();

            net_read_bio_ = ::BIO_new(::BIO_s_mem());
            net_write_bio_ = ::BIO_new(::BIO_s_mem());

            if (!net_read_bio_ || !net_write_bio_)
            {
                if (net_read_bio_)
                    ::BIO_free(net_read_bio_);
                if (net_write_bio_)
                    ::BIO_free(net_write_bio_);
                ::SSL_free(ssl_);
                throw std::bad_alloc();
            }

            ::SSL_set_bio(ssl_, net_read_bio_, net_write_bio_);
        }

        ~stream() noexcept
        {
            if (ssl_)
                ::SSL_free(ssl_); // This automatically frees the attached BIOs
        }

        stream(const stream&) = delete;
        stream& operator=(const stream&) = delete;

        stream(stream&& other) noexcept:
            inner_(std::move(other.inner_)),
            ssl_(std::exchange(other.ssl_, nullptr)),
            net_read_bio_(std::exchange(other.net_read_bio_, nullptr)),
            net_write_bio_(std::exchange(other.net_write_bio_, nullptr))
        {
        }

        stream& operator=(stream&&) noexcept = delete;

        void set_connect_state() noexcept { ::SSL_set_connect_state(ssl_); }

        void set_accept_state() noexcept { ::SSL_set_accept_state(ssl_); }

        /// @brief Configures ALPN protocols in wire format (len-prefixed list), e.g. {2, 'h', '2'}.
        [[nodiscard]] std::expected<void, std::error_code> set_alpn_protocols(std::span<const std::uint8_t> protocols) noexcept
        {
            if (!ssl_ || protocols.empty())
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));

            const int rc = ::SSL_set_alpn_protos(ssl_, protocols.data(), static_cast<unsigned>(protocols.size()));
            if (rc != 0)
                return std::unexpected(std::make_error_code(std::errc::protocol_error));

            return std::expected<void, std::error_code> {};
        }

        /// @brief Returns selected ALPN protocol after handshake, or empty view when none negotiated.
        [[nodiscard]] std::string_view selected_alpn() const noexcept
        {
            const unsigned char* data = nullptr;
            unsigned len {};
            ::SSL_get0_alpn_selected(ssl_, &data, &len);
            return {reinterpret_cast<const char*>(data), len};
        }

        /// @brief Returns the underlying OpenSSL/BoringSSL SSL structure.
        ::SSL* native_handle() noexcept { return ssl_; }

        [[nodiscard]] task<std::expected<void, std::error_code>> handshake() noexcept(false)
        {
            while (true)
            {
                const int ret = ::SSL_do_handshake(ssl_);
                if (ret == 1)
                {
                    // Handshake success, pump any remaining output writes
                    co_await pump_write();
                    co_return std::expected<void, std::error_code> {};
                }

                const int err = ::SSL_get_error(ssl_, ret);
                if (err == SSL_ERROR_WANT_READ)
                {
                    auto w_res = co_await pump_write();
                    if (!w_res)
                        co_return std::unexpected(w_res.error());

                    auto r_res = co_await pump_read();
                    if (!r_res)
                        co_return std::unexpected(r_res.error());
                }
                else if (err == SSL_ERROR_WANT_WRITE)
                {
                    auto w_res = co_await pump_write();
                    if (!w_res)
                        co_return std::unexpected(w_res.error());
                }
                else
                {
                    co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
                }
            }
        }

        [[nodiscard]] result_task read(std::span<char> buffer) noexcept(false)
        {
            while (true)
            {
                const int ret = ::SSL_read(ssl_, buffer.data(), static_cast<int>(buffer.size()));
                if (ret > 0)
                {
                    co_return static_cast<std::size_t>(ret);
                }

                const int err = ::SSL_get_error(ssl_, ret);
                if (err == SSL_ERROR_WANT_READ)
                {
                    auto r_res = co_await pump_read();
                    if (!r_res)
                        co_return std::unexpected(r_res.error());
                }
                else if (err == SSL_ERROR_WANT_WRITE)
                {
                    auto w_res = co_await pump_write();
                    if (!w_res)
                        co_return std::unexpected(w_res.error());
                }
                else if (err == SSL_ERROR_ZERO_RETURN)
                {
                    co_return 0u;
                }
                else
                {
                    co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
                }
            }
        }

        [[nodiscard]] result_task write(std::span<const char> buffer) noexcept(false)
        {
            while (true)
            {
                const int ret = ::SSL_write(ssl_, buffer.data(), static_cast<int>(buffer.size()));
                if (ret > 0)
                {
                    auto w_res = co_await pump_write();
                    if (!w_res)
                        co_return std::unexpected(w_res.error());

                    co_return static_cast<std::size_t>(ret);
                }

                const int err = ::SSL_get_error(ssl_, ret);
                if (err == SSL_ERROR_WANT_READ)
                {
                    auto r_res = co_await pump_read();
                    if (!r_res)
                        co_return std::unexpected(r_res.error());
                }
                else if (err == SSL_ERROR_WANT_WRITE)
                {
                    auto w_res = co_await pump_write();
                    if (!w_res)
                        co_return std::unexpected(w_res.error());
                }
                else
                {
                    co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
                }
            }
        }

        [[nodiscard]] task<std::expected<void, std::error_code>> write_all(std::span<const char> buffer) noexcept(false)
        {
            std::size_t written{};
            while (written < buffer.size())
            {
                auto res = co_await write(buffer.subspan(written));
                if (!res)
                    co_return std::unexpected(res.error());
                if (*res == 0)
                    co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
                written += *res;
            }
            co_return std::expected<void, std::error_code> {};
        }

        [[nodiscard]] InnerStream& inner() noexcept { return *inner_; }
        [[nodiscard]] const InnerStream& inner() const noexcept { return *inner_; }

    private:
        [[nodiscard]] task<std::expected<void, std::error_code>> pump_read() noexcept(false)
        {
            char buf[8192];
            const auto res = co_await inner_->read(std::span {buf, sizeof(buf)});
            if (!res)
                co_return std::unexpected(res.error());

            if (*res > 0)
            {
                ::BIO_write(net_read_bio_, buf, static_cast<int>(*res));
            }
            else
            {
                co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
            }

            co_return std::expected<void, std::error_code> {};
        }

        [[nodiscard]] task<std::expected<void, std::error_code>> pump_write() noexcept(false)
        {
            char buf[8192];
            while (::BIO_ctrl_pending(net_write_bio_) > 0)
            {
                const int read_bytes = ::BIO_read(net_write_bio_, buf, static_cast<int>(sizeof(buf)));
                if (read_bytes > 0)
                {
                    const auto res = co_await inner_->write_all(std::span {buf, static_cast<std::size_t>(read_bytes)});
                    if (!res)
                        co_return std::unexpected(res.error());
                }
            }
            co_return std::expected<void, std::error_code> {};
        }

        std::optional<InnerStream> inner_;
        ::SSL* ssl_ {};
        ::BIO* net_read_bio_ {};
        ::BIO* net_write_bio_ {};
    };

} // namespace kmx::aio::tls
