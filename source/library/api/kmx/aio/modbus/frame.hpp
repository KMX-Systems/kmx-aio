/// @file aio/modbus/frame.hpp
/// @brief Modbus TCP ADU/PDU encode and decode utilities.
/// @details
/// All functions are pure and stateless — they operate on caller-provided
/// buffers and spans with no I/O or heap allocation.  Big-endian byte order
/// is used throughout, as required by the Modbus Application Protocol
/// Specification V1.1b3 §4.3.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstdint>
    #include <expected>
    #include <span>
    #include <system_error>
    #include <vector>
#endif

#include <kmx/aio/modbus/error.hpp>
#include <kmx/aio/modbus/types.hpp>

namespace kmx::aio::modbus::frame
{
    // =========================================================================
    // Constants
    // =========================================================================

    /// @brief MBAP header size in bytes.
    inline constexpr std::size_t mbap_size = 7u;
    /// @brief Maximum PDU payload size in bytes (Modbus spec §4.1).
    inline constexpr std::size_t max_pdu_size = 253u;
    /// @brief Maximum ADU size: MBAP header (7) + PDU (253).
    inline constexpr std::size_t max_adu_size = mbap_size + max_pdu_size;
    /// @brief Maximum readable register or coil count per request (§6.3).
    inline constexpr std::uint16_t max_read_registers = 125u;
    /// @brief Maximum writable register count per request (§6.12).
    inline constexpr std::uint16_t max_write_registers = 123u;
    /// @brief Maximum readable coil count per request (§6.1).
    inline constexpr std::uint16_t max_read_coils = 2000u;
    /// @brief Maximum writable coil count per request (§6.11).
    inline constexpr std::uint16_t max_write_coils = 1968u;
    /// @brief Exception PDU function code flag — high bit set on the request fc.
    inline constexpr std::uint8_t exception_fc_flag = 0x80u;

    // =========================================================================
    // MBAP header encode / decode
    // =========================================================================

    /// @brief Encode a Modbus MBAP header into the first 7 bytes of @p dest.
    /// @param dest Buffer of at least 7 bytes.
    /// @param tid Transaction identifier.
    /// @param pdu_length Byte count of the PDU that follows (NOT including unit_id).
    /// @param unit_id Unit / slave identifier.
    void encode_mbap(std::span<std::uint8_t> dest, std::uint16_t tid,
                     std::uint16_t pdu_length, std::uint8_t unit_id) noexcept;

    /// @brief Decode a Modbus MBAP header from the first 7 bytes of @p src.
    /// @param src Buffer of at least 7 bytes.
    /// @return Decoded header or @c error::malformed_frame when the protocol
    ///         identifier is non-zero.
    [[nodiscard]] std::expected<mbap_header, std::error_code>
    decode_mbap(std::span<const std::uint8_t> src) noexcept;

    // =========================================================================
    // Read request PDU builders (client → server)
    // =========================================================================

    /// @brief Build a Read Coils / Read Discrete Inputs / Read Holding Registers /
    ///        Read Input Registers request PDU.
    /// @param fc Function code (must be one of the four read codes).
    /// @param address Starting address.
    /// @param count Number of items to read.
    /// @return 5-byte PDU [fc, addr_hi, addr_lo, count_hi, count_lo], or error when
    ///         @p count exceeds the protocol maximum for that function code.
    [[nodiscard]] std::expected<std::array<std::uint8_t, 5>, std::error_code>
    encode_read_request(function_code fc, std::uint16_t address,
                        std::uint16_t count) noexcept;

    // =========================================================================
    // Write request PDU builders
    // =========================================================================

    /// @brief Build a Write Single Register request PDU (function code 0x06).
    /// @return 5-byte PDU.
    [[nodiscard]] std::array<std::uint8_t, 5>
    encode_write_single_register(std::uint16_t address, std::uint16_t value) noexcept;

    /// @brief Build a Write Single Coil request PDU (function code 0x05).
    /// @details Encodes @p on as 0xFF00 (ON) or 0x0000 (OFF) per spec §6.5.
    /// @return 5-byte PDU.
    [[nodiscard]] std::array<std::uint8_t, 5>
    encode_write_single_coil(std::uint16_t address, bool on) noexcept;

    /// @brief Build a Write Multiple Registers request PDU (function code 0x10).
    /// @param address Starting address.
    /// @param values Register values to write.
    /// @return Encoded PDU bytes, or @c error::frame_too_large when
    ///         @p values has more than @ref max_write_registers entries.
    [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::error_code>
    encode_write_multiple_registers(std::uint16_t address,
                                    std::span<const std::uint16_t> values) noexcept;

    /// @brief Build a Write Multiple Coils request PDU (function code 0x0F).
    /// @param address Starting address.
    /// @param values Coil values to write (0 = OFF, non-zero = ON).
    /// @return Encoded PDU bytes, or @c error::frame_too_large when
    ///         @p values has more than @ref max_write_coils entries.
    [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::error_code>
    encode_write_multiple_coils(std::uint16_t address,
                                std::span<const std::uint8_t> values) noexcept;

    // =========================================================================
    // Response PDU decoders (server → client)
    // =========================================================================

    /// @brief Check whether a response PDU carries an exception.
    /// @param pdu Raw PDU bytes starting with the function code byte.
    /// @return @c true when the high bit of the function code is set.
    [[nodiscard]] bool is_exception_pdu(std::span<const std::uint8_t> pdu) noexcept;

    /// @brief Decode an exception response PDU.
    /// @param pdu PDU bytes (must begin with an exception function code byte).
    /// @return The exception code, or @c error::malformed_frame when the PDU is
    ///         too short.
    [[nodiscard]] std::expected<exception_code, std::error_code>
    decode_exception_pdu(std::span<const std::uint8_t> pdu) noexcept;

    /// @brief Decode a Read Holding Registers or Read Input Registers response PDU.
    /// @param pdu Raw PDU bytes (function code byte included).
    /// @param expected_fc Function code expected at pdu[0].
    /// @param count Number of registers that were requested.
    /// @return Decoded register values, or an error.
    [[nodiscard]] std::expected<register_values, std::error_code>
    decode_read_registers_response(std::span<const std::uint8_t> pdu,
                                   function_code expected_fc,
                                   std::uint16_t count) noexcept;

    /// @brief Decode a Read Coils or Read Discrete Inputs response PDU.
    /// @param pdu Raw PDU bytes (function code byte included).
    /// @param expected_fc Function code expected at pdu[0].
    /// @param count Number of coils that were requested.
    /// @return Decoded coil values (one byte each, 0 or 1), or an error.
    [[nodiscard]] std::expected<coil_values, std::error_code>
    decode_read_coils_response(std::span<const std::uint8_t> pdu,
                               function_code expected_fc,
                               std::uint16_t count) noexcept;

    /// @brief Decode a Write Single Register or Write Single Coil response PDU.
    /// @details The server echoes the address and value — this function validates
    ///          the echo has the expected function code.
    [[nodiscard]] std::expected<void, std::error_code>
    decode_write_single_response(std::span<const std::uint8_t> pdu,
                                 function_code expected_fc) noexcept;

    /// @brief Decode a Write Multiple Registers or Write Multiple Coils response PDU.
    [[nodiscard]] std::expected<void, std::error_code>
    decode_write_multiple_response(std::span<const std::uint8_t> pdu,
                                   function_code expected_fc) noexcept;

} // namespace kmx::aio::modbus::frame
