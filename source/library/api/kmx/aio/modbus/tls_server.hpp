/// @file aio/modbus/tls_server.hpp
/// @brief Asynchronous Modbus/TLS server with mutual TLS support.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#if defined(KMX_AIO_FEATURE_MODBUS)
    #ifndef PCH
        #include <cstdint>
        #include <expected>
        #include <functional>
        #include <memory>
        #include <system_error>
        #include <vector>
    #endif

    #include <kmx/aio/modbus/types.hpp>
    #include <kmx/aio/task.hpp>

namespace kmx::aio::readiness
{
    class executor;
}

namespace kmx::aio::modbus
{
    /// @brief Asynchronous Modbus/TLS server.
    /// @details
    /// Provides the same handler-registration API as @ref server but accepts
    /// connections over TLS.  When @c tls_config::verify_peer is @c true, the
    /// server requires a valid client certificate (mTLS), rejecting connections
    /// whose certificate cannot be verified against the configured CA.
    class tls_server
    {
    public:
        tls_server() noexcept;
        ~tls_server() noexcept;

        tls_server(const tls_server&) = delete;
        tls_server& operator=(const tls_server&) = delete;
        tls_server(tls_server&&) noexcept;
        tls_server& operator=(tls_server&&) noexcept;

        /// @brief Register a handler for a specific function code.
        void set_handler(function_code fc, request_handler handler);

        /// @brief Start accepting TLS connections and serving requests.
        /// @param exec   Executor that drives accept and connection coroutines.
        /// @param config Server bind address, port, and unit identifier.
        /// @param tls    Certificate, key, and CA for TLS and optional mTLS.
        [[nodiscard]] task<std::expected<void, std::error_code>>
        serve(readiness::executor& exec, server_config config,
              tls_config tls) noexcept(false);

        /// @brief Signal the server to stop accepting new connections.
        void stop() noexcept;

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

} // namespace kmx::aio::modbus
#endif // KMX_AIO_FEATURE_MODBUS
