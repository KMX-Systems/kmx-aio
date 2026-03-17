/// @file aio/completion/tls/stream.hpp
/// @brief Completion-model TLS stream using BoringSSL Memory BIOs over io_uring.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <expected>
    #include <memory>
    #include <span>
    #include <system_error>

    #include <openssl/bio.h>
    #include <openssl/err.h>
    #include <openssl/ssl.h>

    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/completion/tcp/stream.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::completion::tls
{
    /// @brief Asynchronous TLS stream for the completion (io_uring) model.
    class stream
    {
    public:
        using result_task = task<std::expected<std::size_t, std::error_code>>;

        stream() noexcept = default;

        /// @brief Constructs a TLS stream from an existing TCP stream and SSL context.
        stream(tcp::stream inner, ::SSL_CTX* ctx) noexcept(false);

        ~stream() noexcept;

        stream(const stream&) = delete;
        stream& operator=(const stream&) = delete;

        stream(stream&& other) noexcept;
        stream& operator=(stream&&) noexcept = delete;

        [[nodiscard]] task<std::expected<void, std::error_code>> handshake() noexcept(false);
        [[nodiscard]] result_task read(std::span<char> buffer) noexcept(false);
        [[nodiscard]] result_task write(std::span<const char> buffer) noexcept(false);

    private:
        [[nodiscard]] task<std::expected<void, std::error_code>> pump_read() noexcept(false);
        [[nodiscard]] task<std::expected<void, std::error_code>> pump_write() noexcept(false);

        tcp::stream inner_;
        ::SSL* ssl_ {};
        ::BIO* net_read_bio_ {};
        ::BIO* net_write_bio_ {};
    };

} // namespace kmx::aio::completion::tls
