/// @file aio/readiness/quic/engine.hpp
/// @brief Readiness-model QUIC engine using lsquic over epoll-based UDP transport.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#if defined(KMX_AIO_FEATURE_QUIC)

#ifndef PCH
    #include <expected>
    #include <functional>
    #include <memory>
    #include <span>
    #include <system_error>

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/quic/settings.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::readiness::quic
{
    /// @brief QUIC engine for the readiness (epoll) model.
    /// @details Drives lsquic over an epoll-notified UDP socket.
    ///          All lsquic callbacks and lifecycle are shared with completion::quic::engine
    ///          via a private base_engine implementation detail.
    class engine
    {
    public:
        /// @brief Callback invoked when a new QUIC stream is accepted.
        using stream_handler_t = std::function<task<void>(std::span<char>)>;

        /// @brief Constructor.
        /// @param exec The readiness executor to bind this engine to.
        explicit engine(executor& exec) noexcept;

        /// @brief Sets the callback for accepted streams.
        void set_stream_handler(stream_handler_t handler) noexcept;

        /// @brief Non-copyable.
        engine(const engine&) = delete;
        /// @brief Non-copyable.
        engine& operator=(const engine&) = delete;

        /// @brief Move constructor.
        engine(engine&&) noexcept = default;
        /// @brief Move assignment is disabled.
        engine& operator=(engine&&) noexcept = delete;

        /// @brief Destructor.
        ~engine() noexcept;

        /// @brief Starts the QUIC engine, binding to the specified address.
        /// @param ip      IP address to bind to.
        /// @param port    Port number to bind to.
        /// @param ssl_ctx BoringSSL SSL_CTX pointer.
        /// @param config  QUIC protocol settings.
        /// @return Success or an error code.
        [[nodiscard]] task<std::expected<void, std::error_code>> start(ip_address_t ip, port_t port, void* ssl_ctx = nullptr, const kmx::aio::quic::settings& config = kmx::aio::quic::settings{}) noexcept(false);

        /// @brief Processes pending QUIC events (called from the event loop).
        /// @return Success or an error code.
        [[nodiscard]] task<std::expected<void, std::error_code>> process() noexcept(false);

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

} // namespace kmx::aio::readiness::quic

#endif // KMX_AIO_FEATURE_QUIC
