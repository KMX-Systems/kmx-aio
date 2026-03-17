/// @file aio/readiness/tls/stream.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/readiness/tls/stream.hpp"

#include "kmx/logger.hpp"

namespace kmx::aio::readiness::tls
{
    stream::stream(tcp::stream inner, ::SSL_CTX* ctx) noexcept(false):
        inner_(std::move(inner))
    {
        ssl_ = ::SSL_new(ctx);
        if (!ssl_)
            throw std::bad_alloc();

        net_read_bio_ = ::BIO_new(::BIO_s_mem());
        net_write_bio_ = ::BIO_new(::BIO_s_mem());

        if (!net_read_bio_ || !net_write_bio_)
        {
            if (net_read_bio_) ::BIO_free(net_read_bio_);
            if (net_write_bio_) ::BIO_free(net_write_bio_);
            ::SSL_free(ssl_);
            throw std::bad_alloc();
        }

        ::SSL_set_bio(ssl_, net_read_bio_, net_write_bio_);
    }

    stream::~stream() noexcept
    {
        if (ssl_)
            ::SSL_free(ssl_); // This automatically frees the attached BIOs
    }

    stream::stream(stream&& other) noexcept:
        inner_(std::move(other.inner_)),
        ssl_(std::exchange(other.ssl_, nullptr)),
        net_read_bio_(std::exchange(other.net_read_bio_, nullptr)),
        net_write_bio_(std::exchange(other.net_write_bio_, nullptr))
    {
    }


    task<std::expected<void, std::error_code>> stream::pump_read() noexcept(false)
    {
        char buf[8192];
        const auto res = co_await inner_.read(std::span{buf, sizeof(buf)});
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

    task<std::expected<void, std::error_code>> stream::pump_write() noexcept(false)
    {
        char buf[8192];
        const int pending = ::BIO_ctrl_pending(net_write_bio_);
        if (pending > 0)
        {
            const int read_bytes = ::BIO_read(net_write_bio_, buf, static_cast<int>(sizeof(buf)));
            if (read_bytes > 0)
            {
                const auto res = co_await inner_.write_all(std::span{buf, static_cast<std::size_t>(read_bytes)});
                if (!res)
                    co_return std::unexpected(res.error());
            }
        }
        co_return std::expected<void, std::error_code> {};
    }

    task<std::expected<void, std::error_code>> stream::handshake() noexcept(false)
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
                if (!w_res) co_return std::unexpected(w_res.error());

                auto r_res = co_await pump_read();
                if (!r_res) co_return std::unexpected(r_res.error());
            }
            else if (err == SSL_ERROR_WANT_WRITE)
            {
                auto w_res = co_await pump_write();
                if (!w_res) co_return std::unexpected(w_res.error());
            }
            else
            {
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            }
        }
    }

    task<std::expected<std::size_t, std::error_code>> stream::read(std::span<char> buffer) noexcept(false)
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
                if (!r_res) co_return std::unexpected(r_res.error());
            }
            else if (err == SSL_ERROR_WANT_WRITE)
            {
                auto w_res = co_await pump_write();
                if (!w_res) co_return std::unexpected(w_res.error());
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

    task<std::expected<std::size_t, std::error_code>> stream::write(std::span<const char> buffer) noexcept(false)
    {
        while (true)
        {
            const int ret = ::SSL_write(ssl_, buffer.data(), static_cast<int>(buffer.size()));
            if (ret > 0)
            {
                auto w_res = co_await pump_write();
                if (!w_res) co_return std::unexpected(w_res.error());

                co_return static_cast<std::size_t>(ret);
            }

            const int err = ::SSL_get_error(ssl_, ret);
            if (err == SSL_ERROR_WANT_READ)
            {
                auto r_res = co_await pump_read();
                if (!r_res) co_return std::unexpected(r_res.error());
            }
            else if (err == SSL_ERROR_WANT_WRITE)
            {
                auto w_res = co_await pump_write();
                if (!w_res) co_return std::unexpected(w_res.error());
            }
            else
            {
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            }
        }
    }

} // namespace kmx::aio::readiness::tls
