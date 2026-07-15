/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/modbus/frame.hpp>

#include <algorithm>
#include <cstring>

namespace kmx::aio::modbus::frame
{
    namespace
    {
        /// @brief Write a 16-bit value in big-endian byte order into @p dest at @p offset.
        void write_be16(std::span<std::uint8_t> dest, const std::size_t offset,
                        const std::uint16_t value) noexcept
        {
            dest[offset]     = static_cast<std::uint8_t>(value >> 8u);
            dest[offset + 1] = static_cast<std::uint8_t>(value & 0xFFu);
        }

        /// @brief Read a 16-bit big-endian value from @p src at @p offset.
        [[nodiscard]] std::uint16_t read_be16(std::span<const std::uint8_t> src,
                                              const std::size_t offset) noexcept
        {
            return static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(src[offset]) << 8u) |
                static_cast<std::uint16_t>(src[offset + 1]));
        }

        /// @brief Return the protocol-maximum readable item count for @p fc.
        [[nodiscard]] std::uint16_t max_read_count(const function_code fc) noexcept
        {
            switch (fc)
            {
                case function_code::read_coils:
                case function_code::read_discrete_inputs:
                    return max_read_coils;
                case function_code::read_holding_registers:
                case function_code::read_input_registers:
                    return max_read_registers;
                default:
                    return 0u;
            }
        }
    }

    // =========================================================================
    // MBAP header
    // =========================================================================

    void encode_mbap(std::span<std::uint8_t> dest, const std::uint16_t tid,
                     const std::uint16_t pdu_length, const std::uint8_t unit_id) noexcept
    {
        // Length field = unit_id byte (1) + PDU
        const std::uint16_t length_field = static_cast<std::uint16_t>(1u + pdu_length);
        write_be16(dest, 0u, tid);          // bytes 0–1: transaction id
        write_be16(dest, 2u, 0u);           // bytes 2–3: protocol id = 0
        write_be16(dest, 4u, length_field); // bytes 4–5: length
        dest[6u] = unit_id;                 // byte  6:   unit id
    }

    std::expected<mbap_header, std::error_code> decode_mbap(
        std::span<const std::uint8_t> src) noexcept
    {
        if (src.size() < mbap_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        const std::uint16_t protocol_id = read_be16(src, 2u);
        if (protocol_id != 0u)
            return std::unexpected(make_error_code(error::malformed_frame));

        mbap_header hdr;
        hdr.transaction_id = read_be16(src, 0u);
        hdr.protocol_id    = protocol_id;
        hdr.length         = read_be16(src, 4u);
        hdr.unit_id        = src[6u];
        return hdr;
    }

    // =========================================================================
    // Read request PDU builders
    // =========================================================================

    std::expected<std::array<std::uint8_t, 5>, std::error_code> encode_read_request(
        const function_code fc, const std::uint16_t address,
        const std::uint16_t count) noexcept
    {
        const std::uint16_t limit = max_read_count(fc);
        if (limit == 0u || count == 0u || count > limit)
            return std::unexpected(make_error_code(error::frame_too_large));

        std::array<std::uint8_t, 5> pdu {};
        pdu[0] = static_cast<std::uint8_t>(fc);
        write_be16(pdu, 1u, address);
        write_be16(pdu, 3u, count);
        return pdu;
    }

    // =========================================================================
    // Write request PDU builders
    // =========================================================================

    std::array<std::uint8_t, 5> encode_write_single_register(
        const std::uint16_t address, const std::uint16_t value) noexcept
    {
        std::array<std::uint8_t, 5> pdu {};
        pdu[0] = static_cast<std::uint8_t>(function_code::write_single_register);
        write_be16(pdu, 1u, address);
        write_be16(pdu, 3u, value);
        return pdu;
    }

    std::array<std::uint8_t, 5> encode_write_single_coil(const std::uint16_t address,
                                                          const bool on) noexcept
    {
        std::array<std::uint8_t, 5> pdu {};
        pdu[0] = static_cast<std::uint8_t>(function_code::write_single_coil);
        write_be16(pdu, 1u, address);
        // ON = 0xFF00, OFF = 0x0000 (Modbus spec §6.5)
        write_be16(pdu, 3u, on ? std::uint16_t {0xFF00u} : std::uint16_t {0x0000u});
        return pdu;
    }

    std::expected<std::vector<std::uint8_t>, std::error_code> encode_write_multiple_registers(
        const std::uint16_t address, const std::span<const std::uint16_t> values) noexcept
    {
        if (values.empty() || values.size() > max_write_registers)
            return std::unexpected(make_error_code(error::frame_too_large));

        const auto n = static_cast<std::uint16_t>(values.size());
        const auto byte_count = static_cast<std::uint8_t>(n * 2u);

        // PDU: fc(1) + addr(2) + count(2) + byte_count(1) + data(n*2)
        std::vector<std::uint8_t> pdu(6u + static_cast<std::size_t>(byte_count));
        pdu[0] = static_cast<std::uint8_t>(function_code::write_multiple_registers);
        write_be16(pdu, 1u, address);
        write_be16(pdu, 3u, n);
        pdu[5] = byte_count;
        for (std::size_t i = 0u; i < values.size(); ++i)
            write_be16(pdu, 6u + i * 2u, values[i]);

        return pdu;
    }

    std::expected<std::vector<std::uint8_t>, std::error_code> encode_write_multiple_coils(
        const std::uint16_t address, const std::span<const std::uint8_t> values) noexcept
    {
        if (values.empty() || values.size() > max_write_coils)
            return std::unexpected(make_error_code(error::frame_too_large));

        const auto n = static_cast<std::uint16_t>(values.size());
        const auto byte_count = static_cast<std::uint8_t>((n + 7u) / 8u);

        // PDU: fc(1) + addr(2) + count(2) + byte_count(1) + packed_bits(byte_count)
        std::vector<std::uint8_t> pdu(6u + static_cast<std::size_t>(byte_count), 0u);
        pdu[0] = static_cast<std::uint8_t>(function_code::write_multiple_coils);
        write_be16(pdu, 1u, address);
        write_be16(pdu, 3u, n);
        pdu[5] = byte_count;

        for (std::size_t i = 0u; i < values.size(); ++i)
        {
            if (values[i] != 0u)
                pdu[6u + i / 8u] |= static_cast<std::uint8_t>(1u << (i % 8u));
        }

        return pdu;
    }

    // =========================================================================
    // Exception PDU
    // =========================================================================

    bool is_exception_pdu(const std::span<const std::uint8_t> pdu) noexcept
    {
        return !pdu.empty() && ((pdu[0] & exception_fc_flag) != 0u);
    }

    std::expected<exception_code, std::error_code> decode_exception_pdu(
        const std::span<const std::uint8_t> pdu) noexcept
    {
        if (pdu.size() < 2u)
            return std::unexpected(make_error_code(error::malformed_frame));
        return static_cast<exception_code>(pdu[1]);
    }

    // =========================================================================
    // Response PDU decoders
    // =========================================================================

    std::expected<register_values, std::error_code> decode_read_registers_response(
        const std::span<const std::uint8_t> pdu, const function_code expected_fc,
        const std::uint16_t count) noexcept
    {
        if (pdu.empty())
            return std::unexpected(make_error_code(error::malformed_frame));

        if (is_exception_pdu(pdu))
            return std::unexpected(make_error_code(error::exception_response));

        if (pdu[0] != static_cast<std::uint8_t>(expected_fc))
            return std::unexpected(make_error_code(error::unexpected_function_code));

        // PDU layout: fc(1) + byte_count(1) + data(byte_count)
        if (pdu.size() < 2u)
            return std::unexpected(make_error_code(error::malformed_frame));

        const std::uint8_t byte_count = pdu[1];
        const std::size_t expected_bytes = static_cast<std::size_t>(count) * 2u;

        if (byte_count != expected_bytes || pdu.size() < 2u + expected_bytes)
            return std::unexpected(make_error_code(error::malformed_frame));

        register_values result;
        result.reserve(count);
        for (std::size_t i = 0u; i < count; ++i)
            result.push_back(read_be16(pdu, 2u + i * 2u));

        return result;
    }

    std::expected<coil_values, std::error_code> decode_read_coils_response(
        const std::span<const std::uint8_t> pdu, const function_code expected_fc,
        const std::uint16_t count) noexcept
    {
        if (pdu.empty())
            return std::unexpected(make_error_code(error::malformed_frame));

        if (is_exception_pdu(pdu))
            return std::unexpected(make_error_code(error::exception_response));

        if (pdu[0] != static_cast<std::uint8_t>(expected_fc))
            return std::unexpected(make_error_code(error::unexpected_function_code));

        if (pdu.size() < 2u)
            return std::unexpected(make_error_code(error::malformed_frame));

        const std::uint8_t byte_count = pdu[1];
        const std::size_t expected_bytes = (static_cast<std::size_t>(count) + 7u) / 8u;

        if (byte_count != expected_bytes || pdu.size() < 2u + expected_bytes)
            return std::unexpected(make_error_code(error::malformed_frame));

        // Unpack bits into one byte per coil (0 or 1)
        coil_values result;
        result.reserve(count);
        for (std::uint16_t i = 0u; i < count; ++i)
        {
            const std::uint8_t byte = pdu[2u + i / 8u];
            result.push_back(static_cast<std::uint8_t>((byte >> (i % 8u)) & 0x01u));
        }

        return result;
    }

    std::expected<void, std::error_code> decode_write_single_response(
        const std::span<const std::uint8_t> pdu, const function_code expected_fc) noexcept
    {
        if (pdu.empty())
            return std::unexpected(make_error_code(error::malformed_frame));

        if (is_exception_pdu(pdu))
            return std::unexpected(make_error_code(error::exception_response));

        if (pdu[0] != static_cast<std::uint8_t>(expected_fc))
            return std::unexpected(make_error_code(error::unexpected_function_code));

        if (pdu.size() < 5u)
            return std::unexpected(make_error_code(error::malformed_frame));

        return {};
    }

    std::expected<void, std::error_code> decode_write_multiple_response(
        const std::span<const std::uint8_t> pdu, const function_code expected_fc) noexcept
    {
        if (pdu.empty())
            return std::unexpected(make_error_code(error::malformed_frame));

        if (is_exception_pdu(pdu))
            return std::unexpected(make_error_code(error::exception_response));

        if (pdu[0] != static_cast<std::uint8_t>(expected_fc))
            return std::unexpected(make_error_code(error::unexpected_function_code));

        // Response: fc(1) + addr(2) + count(2)
        if (pdu.size() < 5u)
            return std::unexpected(make_error_code(error::malformed_frame));

        return {};
    }

} // namespace kmx::aio::modbus::frame
