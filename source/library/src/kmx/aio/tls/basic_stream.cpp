/// @file aio/tls/basic_stream.cpp
/// @brief The single compiled copy of the TLS handshake and record loops.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/tls/basic_stream.hpp>

#ifndef PCH
    #include <new>
    #include <utility>

    #include <openssl/bio.h>
    #include <openssl/ssl.h>

    #include <kmx/aio/tls/detail/tls_syscalls.hpp>
#endif

namespace kmx::aio::tls
{
    basic_stream::basic_stream(::SSL_CTX* const ctx) noexcept(false)
    {
        ssl_ = ::SSL_new(ctx);
        if (!ssl_)
            throw std::bad_alloc();

        net_read_bio_ = detail::tls_syscalls::bio_new(::BIO_s_mem());
        net_write_bio_ = detail::tls_syscalls::bio_new(::BIO_s_mem());

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

    basic_stream::basic_stream(basic_stream&& other) noexcept:
        ssl_(std::exchange(other.ssl_, nullptr)),
        net_read_bio_(std::exchange(other.net_read_bio_, nullptr)),
        net_write_bio_(std::exchange(other.net_write_bio_, nullptr))
    {
    }

    basic_stream::~basic_stream() noexcept
    {
        if (ssl_)
            ::SSL_free(ssl_); // This automatically frees the attached BIOs
    }

    void basic_stream::set_connect_state() noexcept
    {
        const std::lock_guard lock(engine_mutex_);
        ::SSL_set_connect_state(ssl_);
    }

    void basic_stream::set_accept_state() noexcept
    {
        const std::lock_guard lock(engine_mutex_);
        ::SSL_set_accept_state(ssl_);
    }

    expected_void_t basic_stream::set_alpn_protocols(const std::span<const std::uint8_t> protocols) noexcept
    {
        if (!ssl_ || protocols.empty())
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));

        const std::lock_guard lock(engine_mutex_);
        const int rc = ::SSL_set_alpn_protos(ssl_, protocols.data(), static_cast<unsigned>(protocols.size()));
        if (rc != 0)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));

        return expected_void_t {};
    }

    std::string_view basic_stream::selected_alpn() const noexcept
    {
        const unsigned char* data = nullptr;
        unsigned len {};
        const std::lock_guard lock(engine_mutex_);
        ::SSL_get0_alpn_selected(ssl_, &data, &len);
        return {reinterpret_cast<const char*>(data), len};
    }

    basic_stream::status_task basic_stream::handshake() noexcept(false)
    {
        while (true)
        {
            // SSL_get_error() reports on the last call made on this SSL by this thread, so it has to be
            // asked inside the same critical section as the call it is reporting on. The fill count is
            // read there too, so it describes the read BIO as this call saw it.
            bool completed {};
            int err {};
            std::uint64_t fills {};
            {
                const std::lock_guard lock(engine_mutex_);
                const int ret = ::SSL_do_handshake(ssl_);
                completed = ret == 1;
                if (!completed)
                    err = ::SSL_get_error(ssl_, ret);

                fills = read_bio_fills_;
            }

            if (completed)
            {
                // Handshake success, pump any remaining output writes. The flush is checked like
                // every other one here: it carries the last handshake record, and dropping its error
                // would report a completed handshake whose final flight never reached the transport.
                auto w_res = co_await pump_write();
                if (!w_res)
                    co_return std::unexpected(w_res.error());

                co_return expected_void_t {};
            }

            switch (err)
            {
                case SSL_ERROR_WANT_READ:
                {
                    auto w_res = co_await pump_write();
                    if (!w_res)
                        co_return std::unexpected(w_res.error());

                    auto r_res = co_await pump_read(fills);
                    if (!r_res)
                        co_return std::unexpected(r_res.error());

                    break;
                }
                case SSL_ERROR_WANT_WRITE:
                {
                    auto w_res = co_await pump_write();
                    if (!w_res)
                        co_return std::unexpected(w_res.error());

                    break;
                }
                default: co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            }
        }
    }

    task_returning_expected_size_t basic_stream::read(const std::span<char> buffer) noexcept(false)
    {
        while (true)
        {
            int ret {};
            int err {};
            std::uint64_t fills {};
            {
                const std::lock_guard lock(engine_mutex_);
                ret = ::SSL_read(ssl_, buffer.data(), static_cast<int>(buffer.size()));
                if (ret <= 0)
                    err = ::SSL_get_error(ssl_, ret);

                fills = read_bio_fills_;
            }

            if (ret > 0)
            {
                co_return static_cast<std::size_t>(ret);
            }

            if (err == SSL_ERROR_WANT_READ)
            {
                auto r_res = co_await pump_read(fills);
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

    task_returning_expected_size_t basic_stream::write(const std::span<const char> buffer) noexcept(false)
    {
        while (true)
        {
            int ret {};
            int err {};
            std::uint64_t fills {};
            {
                const std::lock_guard lock(engine_mutex_);
                ret = ::SSL_write(ssl_, buffer.data(), static_cast<int>(buffer.size()));
                if (ret <= 0)
                    err = ::SSL_get_error(ssl_, ret);

                fills = read_bio_fills_;
            }

            if (ret > 0)
            {
                auto w_res = co_await pump_write();
                if (!w_res)
                    co_return std::unexpected(w_res.error());

                co_return static_cast<std::size_t>(ret);
            }

            if (err == SSL_ERROR_WANT_READ)
            {
                auto r_res = co_await pump_read(fills);
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

    basic_stream::status_task basic_stream::write_all(const std::span<const char> buffer) noexcept(false)
    {
        std::size_t written {};
        while (written < buffer.size())
        {
            auto res = co_await write(buffer.subspan(written));
            if (!res)
                co_return std::unexpected(res.error());
            if (*res == 0)
                co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
            written += *res;
        }
        co_return expected_void_t {};
    }

    basic_stream::status_task basic_stream::pump_read(const std::uint64_t seen_fills) noexcept(false)
    {
        constexpr std::size_t buffer_size {8192u};

        // Held across the transport read below, which is the whole point: a socket read and the BIO
        // write that files what it returned have to be one step. Two coroutines each taking a slice of
        // the byte stream and handing them to OpenSSL in whatever order they finished would leave the
        // session decrypting garbage.
        const async_mutex::guard pump_guard = co_await read_pump_mutex_.lock();

        // Whoever held the lock a moment ago may already have brought in what this caller was missing.
        // Returning here rather than reading again keeps a writer that only needed a key update from
        // parking on a socket that has nothing more to say. The count, not the BIO's pending bytes, is
        // what decides: see pump_read()'s declaration.
        {
            const std::lock_guard lock(engine_mutex_);
            if (read_bio_fills_ != seen_fills)
                co_return expected_void_t {};
        }

        char buf[buffer_size];
        const auto res = co_await read_inner(std::span {buf, buffer_size});
        if (!res)
            co_return std::unexpected(res.error());

        if (*res > 0)
        {
            const std::lock_guard lock(engine_mutex_);
            ::BIO_write(net_read_bio_, buf, static_cast<int>(*res));
            ++read_bio_fills_;
        }
        else
        {
            co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
        }

        co_return expected_void_t {};
    }

    basic_stream::status_task basic_stream::pump_write() noexcept(false)
    {
        constexpr std::size_t buffer_size {8192u};

        // Held across the transport write below, so that a record taken out of the write BIO reaches
        // the wire before the next one is taken. Records interleaved on the socket are as fatal to the
        // peer's session as reordered input bytes are to this one.
        const async_mutex::guard pump_guard = co_await write_pump_mutex_.lock();

        char buf[buffer_size];
        while (true)
        {
            int read_bytes {};
            {
                const std::lock_guard lock(engine_mutex_);
                if (::BIO_ctrl_pending(net_write_bio_) > 0)
                    read_bytes = ::BIO_read(net_write_bio_, buf, static_cast<int>(buffer_size));
            }

            // Nothing pending, or a BIO that reported pending bytes and then would not hand them over.
            // The original loop re-tested the pending count and spun forever on the second case.
            if (read_bytes <= 0)
                break;

            const auto res = co_await write_all_inner(std::span {buf, static_cast<std::size_t>(read_bytes)});
            if (!res)
                co_return std::unexpected(res.error());
        }

        co_return expected_void_t {};
    }

} // namespace kmx::aio::tls
