/// @file aio/modbus/types.hpp
/// @brief Modbus Application Protocol types, configuration, and data primitives.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <chrono>
    #include <cstdint>
    #include <string>
    #include <vector>
#endif

namespace kmx::aio::modbus
{
    /// @brief Modbus function codes (Modbus Application Protocol Specification V1.1b3, §6).
    enum class function_code : std::uint8_t
    {
        /// @brief Read 1–2000 contiguous coil output statuses.
        read_coils = 0x01,
        /// @brief Read 1–2000 contiguous discrete input statuses.
        read_discrete_inputs = 0x02,
        /// @brief Read 1–125 contiguous holding register values.
        read_holding_registers = 0x03,
        /// @brief Read 1–125 contiguous input register values.
        read_input_registers = 0x04,
        /// @brief Write a single coil output (force ON or OFF).
        write_single_coil = 0x05,
        /// @brief Write a single holding register.
        write_single_register = 0x06,
        /// @brief Write 1–1968 contiguous coil outputs.
        write_multiple_coils = 0x0F,
        /// @brief Write 1–123 contiguous holding registers.
        write_multiple_registers = 0x10,
    };

    /// @brief Modbus exception codes returned in exception PDUs (§7).
    enum class exception_code : std::uint8_t
    {
        /// @brief Function code not supported by the server.
        illegal_function = 0x01,
        /// @brief Data address not allowed.
        illegal_data_address = 0x02,
        /// @brief Value in request data field is not valid.
        illegal_data_value = 0x03,
        /// @brief Unrecoverable server failure.
        server_device_failure = 0x04,
        /// @brief Request accepted but processing will take time.
        acknowledge = 0x05,
        /// @brief Server is busy and cannot process the request.
        server_device_busy = 0x06,
        /// @brief Memory parity error during read.
        memory_parity_error = 0x08,
        /// @brief Gateway unavailable or misconfigured.
        gateway_path_unavailable = 0x0A,
        /// @brief Target device failed to respond.
        gateway_target_no_response = 0x0B,
    };

    /// @brief Modbus TCP application data unit header (6 bytes, big-endian).
    struct mbap_header
    {
        /// @brief Transaction identifier — echoed in response.
        std::uint16_t transaction_id = 0u;
        /// @brief Protocol identifier — always 0x0000 for Modbus.
        std::uint16_t protocol_id = 0u;
        /// @brief Byte count of unit_id + PDU that follows.
        std::uint16_t length = 0u;
        /// @brief Unit identifier (slave device address).
        std::uint8_t unit_id = 1u;
    };

    /// @brief Register values (each 16-bit, big-endian on the wire).
    using register_values = std::vector<std::uint16_t>;

    /// @brief Coil / discrete-input values.
    /// @details One byte per coil with value 0 (OFF) or 1 (ON).
    /// Stored unpacked for ergonomic use; encoding to/from the Modbus
    /// bit-packed wire format is handled by frame functions.
    using coil_values = std::vector<std::uint8_t>;

    /// @brief Configuration for a plain TCP Modbus client connection.
    struct client_config
    {
        /// @brief Target host name or dotted-decimal IPv4 address.
        std::string host;
        /// @brief TCP port of the Modbus server (default: 502).
        std::uint16_t port = 502u;
        /// @brief Unit identifier sent with every request.
        std::uint8_t unit_id = 1u;
        /// @brief Connect and request timeout.
        std::chrono::milliseconds timeout {1000};
    };

    /// @brief Configuration for a plain TCP Modbus server listener.
    struct server_config
    {
        /// @brief Bind address (empty = all interfaces).
        std::string bind_address;
        /// @brief TCP listen port (default: 502).
        std::uint16_t port = 502u;
        /// @brief Unit identifier this server responds to (0xFF = all).
        std::uint8_t unit_id = 1u;
    };

    /// @brief TLS configuration shared by @c tls_client and @c tls_server.
    struct tls_config
    {
        /// @brief Path to the local certificate in PEM format.
        std::string cert_path;
        /// @brief Path to the private key matching @ref cert_path, in PEM format.
        std::string key_path;
        /// @brief Path to the trusted CA certificate used to verify the peer.
        std::string ca_cert_path;
        /// @brief Whether to require and verify a peer certificate (mTLS when true).
        bool verify_peer = true;
        /// @brief Optional SNI hostname sent by a client during the TLS handshake.
        std::string sni_hostname;
    };

} // namespace kmx::aio::modbus
