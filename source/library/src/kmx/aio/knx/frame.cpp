/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/frame.hpp>

namespace kmx::aio::knx::frame
{
    std::expected<communication_header, std::error_code> decode_communication_header(const cspan_uint8_t buf) noexcept
    {
        if (buf.size() < communication_header_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        KMX_AIO_EXPECTS(buf.size() >= communication_header_size);

        communication_header hdr {};
        if (buf[0] != communication_header_size)
            return std::unexpected(make_error_code(error::malformed_frame));
        if (buf[1] != 0x10u)
            return std::unexpected(make_error_code(error::unsupported_service));

        hdr.protocol_version = buf[1];
        hdr.service_type = static_cast<std::uint16_t>((static_cast<std::uint16_t>(buf[2]) << 8u) |
                                                    static_cast<std::uint16_t>(buf[3]));
        hdr.total_length = static_cast<std::uint16_t>((static_cast<std::uint16_t>(buf[4]) << 8u) | static_cast<std::uint16_t>(buf[5]));
        if (hdr.total_length < communication_header_size)
            return std::unexpected(make_error_code(error::malformed_frame));
        return hdr;
    }

    expected_void_t encode_communication_header(const span_uint8_t dest,
                                                                    const std::uint16_t service_type,
                                                                    const std::uint16_t total_length,
                                                                    const std::uint8_t protocol_version) noexcept
    {
        if ((dest.size() < communication_header_size) || (total_length < communication_header_size))
            return std::unexpected(make_error_code(error::invalid_length));
        if (protocol_version != 0x10u)
            return std::unexpected(make_error_code(error::unsupported_service));

        KMX_AIO_EXPECTS(dest.size() >= communication_header_size);

        dest[0] = static_cast<std::uint8_t>(communication_header_size);
        dest[1] = protocol_version;
        dest[2] = static_cast<std::uint8_t>((service_type >> 8u) & 0xFFu);
        dest[3] = static_cast<std::uint8_t>(service_type & 0xFFu);
        dest[4] = static_cast<std::uint8_t>((total_length >> 8u) & 0xFFu);
        dest[5] = static_cast<std::uint8_t>(total_length & 0xFFu);
        return {};
    }

    std::expected<cemi_frame, std::error_code> decode_cemi(const cspan_uint8_t buf) noexcept
    {
        // The cEMI layer is the constexpr one and reports knx::error; this is the single place the
        // KNXnet/IP framing layer converts that into the std::error_code its own callers expect.
        const auto decoded = knx::cemi::decode(buf);
        if (!decoded.has_value())
            return std::unexpected(make_error_code(decoded.error()));

        return decoded.value();
    }

    expected_void_t encode_tunnelling_request(const span_uint8_t dest,
                                                                   const std::uint8_t channel_id,
                                                                   const std::uint8_t sequence_number,
                                                                   const cspan_uint8_t cemi_bytes) noexcept
    {
        if (channel_id == 0u)
            return std::unexpected(make_error_code(error::invalid_configuration));
        if (dest.size() < (tunnelling_request_header_size + cemi_bytes.size()))
            return std::unexpected(make_error_code(error::invalid_length));

        if (cemi_bytes.size() < cemi_min_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        KMX_AIO_EXPECTS(dest.size() >= (tunnelling_request_header_size + cemi_bytes.size()));

        dest[0] = channel_id;
        dest[1] = sequence_number;
        dest[2] = 0x00u;
        dest[3] = 0x00u;

        for (std::size_t i = 0u; i < cemi_bytes.size(); ++i)
            dest[tunnelling_request_header_size + i] = cemi_bytes[i];

        return {};
    }

    std::expected<tunnelling_request_frame, std::error_code> decode_tunnelling_request(const cspan_uint8_t buf) noexcept
    {
        if (buf.size() < tunnelling_request_header_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto channel_id = buf[0];
        const auto sequence_number = buf[1];
        if (channel_id == 0u)
            return std::unexpected(make_error_code(error::malformed_frame));
        if ((buf[2] != 0u) || (buf[3] != 0u))
            return std::unexpected(make_error_code(error::malformed_frame));
        const auto message_length = static_cast<std::uint16_t>(buf.size() - tunnelling_request_header_size);

        if (message_length < cemi_min_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        const cspan_uint8_t cemi_span { buf.data() + tunnelling_request_header_size, message_length };
        const auto cemi = decode_cemi(cemi_span);
        if (!cemi.has_value())
            return std::unexpected(cemi.error());

        tunnelling_request_frame decoded {};
        decoded.channel_id = channel_id;
        decoded.sequence_number = sequence_number;
        decoded.message_length = message_length;
        decoded.cemi = cemi.value();
        decoded.cemi_bytes.size = static_cast<std::uint16_t>(cemi_span.size());
        for (std::size_t i = 0u; i < cemi_span.size(); ++i)
            decoded.cemi_bytes.bytes[i] = cemi_span[i];
        return decoded;
    }

    std::expected<tunnelling_ack_frame, std::error_code> decode_tunnelling_ack(const cspan_uint8_t buf) noexcept
    {
        if (buf.size() != tunnelling_ack_size)
            return std::unexpected(make_error_code(error::malformed_frame));
        if (buf[3] != 0u)
            return std::unexpected(make_error_code(error::malformed_frame));

        tunnelling_ack_frame decoded {};
        if (buf[0] == 0u)
            return std::unexpected(make_error_code(error::malformed_frame));
        decoded.channel_id = buf[0];
        decoded.sequence_number = buf[1];
        decoded.status = buf[2];
        return decoded;
    }

    expected_void_t encode_tunnelling_request_packet(const span_uint8_t dest,
                                                                         const std::uint8_t channel_id,
                                                                         const std::uint8_t sequence_number,
                                                                         const cspan_uint8_t cemi_bytes) noexcept
    {
        const auto body_length = tunnelling_request_header_size + cemi_bytes.size();
        const auto total_length = communication_header_size + body_length;
        if ((total_length > max_frame_size) || (dest.size() < total_length))
            return std::unexpected(make_error_code(error::invalid_length));

        const auto header = encode_communication_header(dest,
                                                        tunnelling_request_service,
                                                        static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        return encode_tunnelling_request({ dest.data() + communication_header_size, body_length },
                                         channel_id,
                                         sequence_number,
                                         cemi_bytes);
    }

    expected_void_t encode_tunnelling_ack_packet(const span_uint8_t dest,
                                                                      const std::uint8_t channel_id,
                                                                      const std::uint8_t sequence_number,
                                                                      const std::uint8_t status) noexcept
    {
        if (channel_id == 0u)
            return std::unexpected(make_error_code(error::invalid_configuration));
        const auto total_length = communication_header_size + tunnelling_ack_size;
        if (dest.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));

        const auto header = encode_communication_header(dest,
                                                        tunnelling_ack_service,
                                                        static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        dest[communication_header_size] = channel_id;
        dest[communication_header_size + 1u] = sequence_number;
        dest[communication_header_size + 2u] = status;
        dest[communication_header_size + 3u] = 0x00u;
        return {};
    }

    std::expected<tunnelling_request_frame, std::error_code> decode_tunnelling_request_packet(const cspan_uint8_t buf) noexcept
    {
        const auto header = decode_communication_header(buf);
        if (!header.has_value())
            return std::unexpected(header.error());

        if (header->protocol_version != 0x10u)
            return std::unexpected(make_error_code(error::unsupported_service));
        if (header->service_type != tunnelling_request_service)
            return std::unexpected(make_error_code(error::unsupported_service));
        if ((header->total_length != buf.size()) || (header->total_length < communication_header_size))
            return std::unexpected(make_error_code(error::malformed_frame));

        return decode_tunnelling_request({ buf.data() + communication_header_size,
                                           buf.size() - communication_header_size });
    }

    std::expected<tunnelling_ack_frame, std::error_code> decode_tunnelling_ack_packet(const cspan_uint8_t buf) noexcept
    {
        const auto header = decode_communication_header(buf);
        if (!header.has_value())
            return std::unexpected(header.error());

        if (header->protocol_version != 0x10u)
            return std::unexpected(make_error_code(error::unsupported_service));
        if (header->service_type != tunnelling_ack_service)
            return std::unexpected(make_error_code(error::unsupported_service));
        if ((header->total_length != buf.size()) || (buf.size() != communication_header_size + tunnelling_ack_size))
            return std::unexpected(make_error_code(error::malformed_frame));

        return decode_tunnelling_ack({ buf.data() + communication_header_size, tunnelling_ack_size });
    }
}
