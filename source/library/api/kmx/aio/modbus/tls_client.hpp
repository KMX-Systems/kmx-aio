/// @file aio/modbus/tls_client.hpp
/// @brief Asynchronous Modbus/TLS (Modbus Security) client facade.
/// @details
/// Implements Modbus over TLS as specified by the Modbus Security Protocol
/// Specification (May 2018).  Mutual TLS (mTLS) is supported for high-security
/// deployments compliant with IEC 62443.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#if defined(KMX_AIO_FEATURE_MODBUS)
    #ifndef PCH
        #include <cstdint>
        #include <expected>
        #include <memory>
        #include <span>
        #include <system_error>
    #endif

    #include <kmx/aio/modbus/types.hpp>
    #include <kmx/aio/task.hpp>

namespace kmx::aio::readiness
{
    class executor;
}

namespace kmx::aio::modbus
{
    /// @brief Asynchronous Modbus/TLS client.
    /// @details
    /// Provides the same coroutine API as @ref client but over a TLS-secured
    /// connection.  When @c tls_config::verify_peer is @c true and
    /// @c tls_config::cert_path / @c tls_config::key_path are set, mutual TLS
    /// (mTLS) authentication is performed.
    ///
    /// @note @c client_config::host must be a dotted-decimal IPv4 address.
    class tls_client
    {
    public:
        /// @brief Construct the TLS client.
        /// @param config    TCP connection and Modbus protocol parameters.
        /// @param tls       Certificate and verification settings.
        /// @param exec      Readiness executor owning the I/O event loop.
        explicit tls_client(client_config config, tls_config tls,
                            readiness::executor& exec) noexcept;
        ~tls_client() noexcept;

        tls_client(const tls_client&) = delete;
        tls_client& operator=(const tls_client&) = delete;
        tls_client(tls_client&&) noexcept;
        tls_client& operator=(tls_client&&) noexcept;

        /// @brief Establish the TCP connection and perform the TLS handshake.
        /// @return Task resolving to success, or an error.
        [[nodiscard]] task<std::expected<void, std::error_code>> connect() noexcept(false);

        /// @brief Close the TLS session and underlying TCP connection.
        [[nodiscard]] task<std::expected<void, std::error_code>> disconnect() noexcept(false);

        [[nodiscard]] task<std::expected<register_values, std::error_code>>
        read_holding_registers(std::uint16_t address, std::uint16_t count) noexcept(false);

        [[nodiscard]] task<std::expected<register_values, std::error_code>>
        read_input_registers(std::uint16_t address, std::uint16_t count) noexcept(false);

        [[nodiscard]] task<std::expected<coil_values, std::error_code>>
        read_coils(std::uint16_t address, std::uint16_t count) noexcept(false);

        [[nodiscard]] task<std::expected<coil_values, std::error_code>>
        read_discrete_inputs(std::uint16_t address, std::uint16_t count) noexcept(false);

        [[nodiscard]] task<std::expected<void, std::error_code>>
        write_single_register(std::uint16_t address, std::uint16_t value) noexcept(false);

        [[nodiscard]] task<std::expected<void, std::error_code>>
        write_single_coil(std::uint16_t address, bool on) noexcept(false);

        [[nodiscard]] task<std::expected<void, std::error_code>>
        write_multiple_registers(std::uint16_t address,
                                 std::span<const std::uint16_t> values) noexcept(false);

        [[nodiscard]] task<std::expected<void, std::error_code>>
        write_multiple_coils(std::uint16_t address,
                             std::span<const std::uint8_t> values) noexcept(false);

        /// @brief Check whether the client currently has an active TLS session.
        [[nodiscard]] bool is_connected() const noexcept;

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

} // namespace kmx::aio::modbus
#endif // KMX_AIO_FEATURE_MODBUS
