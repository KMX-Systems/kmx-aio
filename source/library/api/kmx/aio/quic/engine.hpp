/// @file aio/quic/engine.hpp
/// @brief Generic QUIC engine template consolidated for all I/O models.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#if defined(KMX_AIO_FEATURE_QUIC)

    #ifndef PCH
        #include <expected>
        #include <functional>
        #include <memory>
        #include <string>
        #include <span>
        #include <system_error>

        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/quic/settings.hpp>
        #include <kmx/aio/task.hpp>
    #endif

namespace kmx::aio::quic
{
    /// @brief Generic QUIC engine template.
    /// @details Provides a unified interface for lsquic-based engines,
    ///          parameterized by Executor and UdpSocket types.
    /// @tparam Executor  The model-specific executor (readiness/completion).
    /// @tparam UdpSocket The model-specific UDP socket.
    template <typename Executor, typename UdpSocket>
    class generic_engine
    {
    public:
        /// @brief Callback invoked when a new QUIC stream is accepted.
        using stream_handler_t = std::function<task<void>(std::span<char>)>;

        /// @brief Constructor.
        /// @param exec The executor to bind this engine to.
        explicit generic_engine(Executor& exec) noexcept;

        /// @brief Sets the callback for accepted streams.
        void set_stream_handler(stream_handler_t handler) noexcept;

        /// @brief Non-copyable.
        generic_engine(const generic_engine&) = delete;
        /// @brief Non-copyable.
        generic_engine& operator=(const generic_engine&) = delete;

        /// @brief Move constructor.
        generic_engine(generic_engine&&) noexcept = default;
        /// @brief Move assignment is disabled.
        generic_engine& operator=(generic_engine&&) noexcept = delete;

        /// @brief Destructor.
        ~generic_engine() noexcept;

        /// @brief Starts the QUIC engine, binding to the specified address.
        /// @param ip      IP address to bind to.
        /// @param port    Port number to bind to.
        /// @param ssl_ctx BoringSSL SSL_CTX pointer.
        /// @param config  QUIC protocol settings.
        /// @return Success or an error code.
        [[nodiscard]] task<std::expected<void, std::error_code>> start(ip_address_t ip, port_t port, void* ssl_ctx = nullptr,
                                                                       const settings& config = settings {}) noexcept(false);

        /// @brief Processes pending QUIC events (called from the event loop).
        /// @return Success or an error code.
        [[nodiscard]] task<std::expected<void, std::error_code>> process() noexcept(false);

        /// @brief Connects the QUIC engine to a specified remote peer.
        /// @param peer_ip   IP address to connect to.
        /// @param peer_port Port number to connect to.
        /// @param hostname  Hostname for SNI (Server Name Indication). Optional.
        /// @param ssl_ctx   BoringSSL SSL_CTX pointer.
        /// @param config    QUIC protocol settings.
        /// @return Success or an error code once connection is established.
        [[nodiscard]] task<std::expected<void, std::error_code>> connect(ip_address_t peer_ip, port_t peer_port,
                                                                         const std::string& hostname = "", void* ssl_ctx = nullptr,
                                                                         const settings& config = settings{}) noexcept(false);

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

} // namespace kmx::aio::quic

#endif // KMX_AIO_FEATURE_QUIC
