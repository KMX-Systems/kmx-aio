/// @file src/kmx/aio/knx/routing.cpp
/// @brief KNXnet/IP ROUTING_INDICATION, ROUTING_BUSY and ROUTING_LOST_MESSAGE codecs.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/routing.hpp>
#ifndef PCH
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/frame.hpp>

    #include <algorithm>
    #include <cstddef>
    #include <cstdint>
#endif

namespace kmx::aio::knx::routing
{
    /// @brief Reads a big-endian 16-bit field.
    [[nodiscard]] static constexpr std::uint16_t decode_u16_be(const cspan_uint8_t value, const std::size_t offset) noexcept
    {
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(value[offset]) << 8u) |
                                          static_cast<std::uint16_t>(value[offset + 1u]));
    }

    /// @brief Writes a big-endian 16-bit field.
    static constexpr void encode_u16_be(const span_uint8_t destination, const std::size_t offset, const std::uint16_t value) noexcept
    {
        destination[offset] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
        destination[offset + 1u] = static_cast<std::uint8_t>(value & 0xFFu);
    }

    /// @brief Validates the header and the information block prologue the two control services share.
    /// @param packet The received datagram.
    /// @param service The service type expected.
    /// @param body_size The exact body size that service defines.
    /// @return Nothing on success, or why the datagram is not that service.
    /// @details Both control services carry a single information block whose first octet repeats its own
    ///          size. A block that disagrees with the datagram it arrived in is rejected rather than read.
    [[nodiscard]] static expected_void_t validate_control_packet(const cspan_uint8_t packet, const std::uint16_t service,
                                                                 const std::size_t body_size) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if (header->service_type != service)
            return std::unexpected(make_error_code(error::unsupported_service));
        if ((header->total_length != packet.size()) || (packet.size() != frame::communication_header_size + body_size))
            return std::unexpected(make_error_code(error::malformed_frame));
        if (packet[frame::communication_header_size] != static_cast<std::uint8_t>(body_size))
            return std::unexpected(make_error_code(error::malformed_frame));
        return {};
    }

    /// @brief Writes the header and information block prologue the two control services share.
    [[nodiscard]] static expected_void_t begin_control_packet(const span_uint8_t destination, const std::uint16_t service,
                                                              const std::size_t body_size, const std::uint8_t device_state) noexcept
    {
        const auto total_length = frame::communication_header_size + body_size;
        if (destination.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));
        const auto header = frame::encode_communication_header(destination, service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());
        destination[frame::communication_header_size] = static_cast<std::uint8_t>(body_size);
        destination[frame::communication_header_size + 1u] = device_state;
        return {};
    }

    expected_void_t encode_indication_packet(const span_uint8_t destination, const indication& value) noexcept
    {
        if (value.cemi_bytes.empty())
            return std::unexpected(make_error_code(error::invalid_configuration));
        const auto total_length = frame::communication_header_size + value.cemi_bytes.size();
        if ((total_length > frame::max_total_length) || (destination.size() < total_length))
            return std::unexpected(make_error_code(error::invalid_length));
        const auto header = frame::encode_communication_header(destination, indication_service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());
        std::copy_n(value.cemi_bytes.begin(), value.cemi_bytes.size(), destination.begin() + frame::communication_header_size);
        return {};
    }

    std::expected<indication, std::error_code> decode_indication_packet(const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->service_type != indication_service) || (header->total_length != packet.size()))
            return std::unexpected(make_error_code(error::unsupported_service));
        if (packet.size() < frame::communication_header_size + frame::cemi_min_size)
            return std::unexpected(make_error_code(error::malformed_frame));
        const auto cemi_bytes = packet.subspan(frame::communication_header_size);
        const auto cemi = frame::decode_cemi(cemi_bytes);
        if (!cemi.has_value())
            return std::unexpected(cemi.error());
        return indication {cemi_bytes, cemi.value()};
    }

    expected_void_t encode_lost_message_packet(const span_uint8_t destination, const lost_message& value) noexcept
    {
        const auto begun = begin_control_packet(destination, lost_message_service, lost_message_body_size, value.device_state);
        if (!begun.has_value())
            return std::unexpected(begun.error());
        encode_u16_be(destination, frame::communication_header_size + 2u, value.count);
        return {};
    }

    std::expected<lost_message, std::error_code> decode_lost_message_packet(const cspan_uint8_t packet) noexcept
    {
        const auto valid = validate_control_packet(packet, lost_message_service, lost_message_body_size);
        if (!valid.has_value())
            return std::unexpected(valid.error());
        return lost_message {
            .device_state = packet[frame::communication_header_size + 1u],
            .count = decode_u16_be(packet, frame::communication_header_size + 2u),
        };
    }

    expected_void_t encode_busy_packet(const span_uint8_t destination, const busy& value) noexcept
    {
        const auto begun = begin_control_packet(destination, busy_service, busy_body_size, value.device_state);
        if (!begun.has_value())
            return std::unexpected(begun.error());
        encode_u16_be(destination, frame::communication_header_size + 2u, value.wait_time_ms);
        encode_u16_be(destination, frame::communication_header_size + 4u, value.control_field);
        return {};
    }

    std::expected<busy, std::error_code> decode_busy_packet(const cspan_uint8_t packet) noexcept
    {
        const auto valid = validate_control_packet(packet, busy_service, busy_body_size);
        if (!valid.has_value())
            return std::unexpected(valid.error());
        return busy {
            .device_state = packet[frame::communication_header_size + 1u],
            .wait_time_ms = decode_u16_be(packet, frame::communication_header_size + 2u),
            .control_field = decode_u16_be(packet, frame::communication_header_size + 4u),
        };
    }
}
