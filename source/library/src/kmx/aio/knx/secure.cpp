/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure.hpp>

#include <kmx/aio/knx/frame.hpp>

namespace kmx::aio::knx::secure
{
    namespace
    {
        [[nodiscard]] constexpr bool supported_profile(const profile value) noexcept
        {
            return (value == profile::ip_secure) || (value == profile::data_secure);
        }

        [[nodiscard]] constexpr std::uint64_t decode_u64_be(const cspan_uint8_t value) noexcept
        {
            return (static_cast<std::uint64_t>(value[0u]) << 56u) |
                   (static_cast<std::uint64_t>(value[1u]) << 48u) |
                   (static_cast<std::uint64_t>(value[2u]) << 40u) |
                   (static_cast<std::uint64_t>(value[3u]) << 32u) |
                   (static_cast<std::uint64_t>(value[4u]) << 24u) |
                   (static_cast<std::uint64_t>(value[5u]) << 16u) |
                   (static_cast<std::uint64_t>(value[6u]) << 8u) |
                   static_cast<std::uint64_t>(value[7u]);
        }

        constexpr void encode_u64_be(const span_uint8_t destination, const std::uint64_t value) noexcept
        {
            destination[0u] = static_cast<std::uint8_t>((value >> 56u) & 0xFFu);
            destination[1u] = static_cast<std::uint8_t>((value >> 48u) & 0xFFu);
            destination[2u] = static_cast<std::uint8_t>((value >> 40u) & 0xFFu);
            destination[3u] = static_cast<std::uint8_t>((value >> 32u) & 0xFFu);
            destination[4u] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
            destination[5u] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
            destination[6u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
            destination[7u] = static_cast<std::uint8_t>(value & 0xFFu);
        }
    }

    std::expected<void, std::error_code> encode_secure_packet(
        const span_uint8_t destination, const packet& value) noexcept
    {
        if (!supported_profile(value.selected))
            return std::unexpected(make_error_code(error::invalid_configuration));

        const auto total_length = frame::communication_header_size + secure_packet_header_size + value.payload.size();
        if ((total_length > frame::max_frame_size) || (destination.size() < total_length))
            return std::unexpected(make_error_code(error::invalid_length));

        const auto header = frame::encode_communication_header(
            destination, secure_service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        destination[frame::communication_header_size] = static_cast<std::uint8_t>(value.selected);
        destination[frame::communication_header_size + 1u] = 0u;
        encode_u64_be(destination.subspan(frame::communication_header_size + 2u, 8u), value.sequence);

        const auto payload_length = static_cast<std::uint16_t>(value.payload.size());
        destination[frame::communication_header_size + 10u] = static_cast<std::uint8_t>((payload_length >> 8u) & 0xFFu);
        destination[frame::communication_header_size + 11u] = static_cast<std::uint8_t>(payload_length & 0xFFu);

        for (std::size_t i = 0u; i < value.payload.size(); ++i)
            destination[frame::communication_header_size + secure_packet_header_size + i] = value.payload[i];

        return {};
    }

    std::expected<packet, std::error_code> decode_secure_packet(const cspan_uint8_t packet_bytes) noexcept
    {
        const auto header = frame::decode_communication_header(packet_bytes);
        if (!header.has_value())
            return std::unexpected(header.error());

        if (header->service_type != secure_service)
            return std::unexpected(make_error_code(error::unsupported_service));
        if (header->total_length != packet_bytes.size())
            return std::unexpected(make_error_code(error::malformed_frame));

        if (packet_bytes.size() < frame::communication_header_size + secure_packet_header_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto selected = static_cast<profile>(packet_bytes[frame::communication_header_size]);
        if (!supported_profile(selected))
            return std::unexpected(make_error_code(error::unsupported_service));
        if (packet_bytes[frame::communication_header_size + 1u] != 0u)
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto sequence = decode_u64_be(packet_bytes.subspan(frame::communication_header_size + 2u, 8u));
        const auto payload_length = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(packet_bytes[frame::communication_header_size + 10u]) << 8u) |
            static_cast<std::uint16_t>(packet_bytes[frame::communication_header_size + 11u]));

        const auto body_offset = frame::communication_header_size + secure_packet_header_size;
        if ((body_offset + payload_length) != packet_bytes.size())
            return std::unexpected(make_error_code(error::malformed_frame));

        packet decoded {
            .selected = selected,
            .sequence = sequence,
            .payload = std::vector<std::uint8_t>(packet_bytes.begin() + body_offset, packet_bytes.end()),
        };
        return decoded;
    }

    std::expected<std::vector<std::uint8_t>, std::error_code> protect_packet(
        provider& crypto,
        const profile selected,
        const std::span<const std::uint8_t> payload,
        const std::uint64_t sequence) noexcept
    {
        if (!supported_profile(selected))
            return std::unexpected(make_error_code(error::invalid_configuration));

        const auto protected_payload = crypto.protect(payload, sequence);
        if (!protected_payload.has_value())
            return std::unexpected(protected_payload.error());

        packet wrapped {
            .selected = selected,
            .sequence = sequence,
            .payload = std::move(*protected_payload),
        };
        std::vector<std::uint8_t> encoded(
            frame::communication_header_size + secure_packet_header_size + wrapped.payload.size(), 0u);
        const auto result = encode_secure_packet(encoded, wrapped);
        if (!result.has_value())
            return std::unexpected(result.error());

        return encoded;
    }

    std::expected<std::vector<std::uint8_t>, std::error_code> unprotect_packet(
        provider& crypto,
        const profile expected_profile,
        const cspan_uint8_t packet_bytes,
        replay_window_state* const replay) noexcept
    {
        if (!supported_profile(expected_profile))
            return std::unexpected(make_error_code(error::invalid_configuration));

        const auto decoded = decode_secure_packet(packet_bytes);
        if (!decoded.has_value())
            return std::unexpected(decoded.error());

        if (decoded->selected != expected_profile)
            return std::unexpected(make_error_code(error::invalid_configuration));

        if ((replay != nullptr) && !replay->accept(decoded->sequence))
            return std::unexpected(make_error_code(error::sequence_error));

        return crypto.unprotect(decoded->payload, decoded->sequence);
    }
}
