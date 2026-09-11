/// @file src/kmx/aio/knx/frame.cpp
/// @brief KNXnet/IP header, tunnelling, device configuration and tunnelling feature frame codecs.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/frame.hpp>
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/cemi.hpp>
    #include <kmx/aio/knx/cemi_bytes_storage.hpp>
    #include <kmx/aio/knx/cemi_frame.hpp>
    #include <kmx/aio/knx/contract.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/tunnelling_feature_value.hpp>

    #include <algorithm>
    #include <cstdint>
    #include <expected>
    #include <system_error>
#endif

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
        hdr.service_type = static_cast<std::uint16_t>((static_cast<std::uint16_t>(buf[2]) << 8u) | static_cast<std::uint16_t>(buf[3]));
        hdr.total_length = static_cast<std::uint16_t>((static_cast<std::uint16_t>(buf[4]) << 8u) | static_cast<std::uint16_t>(buf[5]));
        if (hdr.total_length < communication_header_size)
            return std::unexpected(make_error_code(error::malformed_frame));
        return hdr;
    }

    expected_void_t encode_communication_header(const span_uint8_t dest, const std::uint16_t service_type, const std::uint16_t total_length,
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

    expected_void_t encode_tunnelling_request(const span_uint8_t dest, const std::uint8_t channel_id, const std::uint8_t sequence_number,
                                              const cspan_uint8_t cemi_bytes) noexcept
    {
        if (channel_id == 0u)
            return std::unexpected(make_error_code(error::invalid_configuration));
        if (dest.size() < (tunnelling_request_header_size + cemi_bytes.size()))
            return std::unexpected(make_error_code(error::invalid_length));

        if (cemi_bytes.size() < cemi_min_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        KMX_AIO_EXPECTS(dest.size() >= (tunnelling_request_header_size + cemi_bytes.size()));

        dest[0] = connection_header_structure_length;
        dest[1] = channel_id;
        dest[2] = sequence_number;
        dest[3] = 0x00u; // reserved in a request

        std::copy_n(cemi_bytes.begin(), cemi_bytes.size(), dest.begin() + tunnelling_request_header_size);

        return {};
    }

    /// @brief Returns the cEMI octets behind a tunnelling connection header.
    /// @param buf The body, starting at the connection header.
    /// @return The message's octets, or why they are not a message this decoder can hold.
    /// @details The upper bound is checked here rather than left to the cEMI decoder. That decoder's own
    ///          length arithmetic already cannot admit a longer message, but this is the bound that keeps
    ///          the copy inside @ref cemi_bytes_storage, so it is stated where the copy is - not inferred
    ///          from another header.
    [[nodiscard]] static std::expected<cspan_uint8_t, std::error_code> tunnelling_cemi_span(const cspan_uint8_t buf) noexcept
    {
        const auto message_length = buf.size() - tunnelling_request_header_size;
        if (message_length < cemi_min_size)
            return std::unexpected(make_error_code(error::malformed_frame));
        if (message_length > cemi_max_size)
            return std::unexpected(make_error_code(error::invalid_length));
        return cspan_uint8_t {buf.data() + tunnelling_request_header_size, message_length};
    }

    std::expected<tunnelling_request_frame, std::error_code> decode_tunnelling_request(const cspan_uint8_t buf) noexcept
    {
        if (buf.size() < tunnelling_request_header_size)
            return std::unexpected(make_error_code(error::malformed_frame));
        if (buf[0] != connection_header_structure_length)
            return std::unexpected(make_error_code(error::malformed_frame));
        if (buf[1] == 0u)
            return std::unexpected(make_error_code(error::malformed_frame));
        if (buf[3] != 0u)
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto cemi_span = tunnelling_cemi_span(buf);
        if (!cemi_span.has_value())
            return std::unexpected(cemi_span.error());
        const auto cemi = decode_cemi(*cemi_span);
        if (!cemi.has_value())
            return std::unexpected(cemi.error());

        tunnelling_request_frame decoded {};
        decoded.channel_id = buf[1];
        decoded.sequence_number = buf[2];
        decoded.message_length = static_cast<std::uint16_t>(cemi_span->size());
        decoded.cemi = cemi.value();
        decoded.cemi_bytes.size = static_cast<std::uint16_t>(cemi_span->size());
        std::copy_n(cemi_span->begin(), cemi_span->size(), decoded.cemi_bytes.bytes.begin());
        return decoded;
    }

    std::expected<tunnelling_ack_frame, std::error_code> decode_tunnelling_ack(const cspan_uint8_t buf) noexcept
    {
        if (buf.size() != tunnelling_ack_size)
            return std::unexpected(make_error_code(error::malformed_frame));
        if (buf[0] != connection_header_structure_length)
            return std::unexpected(make_error_code(error::malformed_frame));
        if (buf[1] == 0u)
            return std::unexpected(make_error_code(error::malformed_frame));

        tunnelling_ack_frame decoded {};
        decoded.channel_id = buf[1];
        decoded.sequence_number = buf[2];
        // The fourth octet is the status in an acknowledgement, not a reserved zero.
        decoded.status = buf[3];
        return decoded;
    }

    expected_void_t encode_tunnelling_request_packet(const span_uint8_t dest, const std::uint8_t channel_id,
                                                     const std::uint8_t sequence_number, const cspan_uint8_t cemi_bytes) noexcept
    {
        const auto body_length = tunnelling_request_header_size + cemi_bytes.size();
        const auto total_length = communication_header_size + body_length;
        if ((total_length > max_total_length) || (dest.size() < total_length))
            return std::unexpected(make_error_code(error::invalid_length));

        const auto header = encode_communication_header(dest, tunnelling_request_service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        return encode_tunnelling_request({dest.data() + communication_header_size, body_length}, channel_id, sequence_number, cemi_bytes);
    }

    expected_void_t encode_tunnelling_ack_packet(const span_uint8_t dest, const std::uint8_t channel_id, const std::uint8_t sequence_number,
                                                 const std::uint8_t status) noexcept
    {
        if (channel_id == 0u)
            return std::unexpected(make_error_code(error::invalid_configuration));
        const auto total_length = communication_header_size + tunnelling_ack_size;
        if (dest.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));

        const auto header = encode_communication_header(dest, tunnelling_ack_service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        dest[communication_header_size] = connection_header_structure_length;
        dest[communication_header_size + 1u] = channel_id;
        dest[communication_header_size + 2u] = sequence_number;
        dest[communication_header_size + 3u] = status;
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

        return decode_tunnelling_request({buf.data() + communication_header_size, buf.size() - communication_header_size});
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

        return decode_tunnelling_ack({buf.data() + communication_header_size, tunnelling_ack_size});
    }

    /// @brief Reads the connection header both tunnelling and device management services begin with.
    /// @param buf The body, starting at the connection header.
    /// @param expect_reserved_zero Whether the fourth octet must be zero, as it is in a request.
    /// @return The channel id, sequence counter and fourth octet, or why the header is malformed.
    [[nodiscard]] static std::expected<tunnelling_ack_frame, std::error_code> decode_connection_header(
        const cspan_uint8_t buf, const bool expect_reserved_zero) noexcept
    {
        if (buf.size() < tunnelling_request_header_size)
            return std::unexpected(make_error_code(error::malformed_frame));
        if (buf[0] != connection_header_structure_length)
            return std::unexpected(make_error_code(error::malformed_frame));
        if (buf[1] == 0u)
            return std::unexpected(make_error_code(error::malformed_frame));
        if (expect_reserved_zero && (buf[3] != 0u))
            return std::unexpected(make_error_code(error::malformed_frame));

        return tunnelling_ack_frame {buf[1], buf[2], buf[3]};
    }

    /// @brief Writes the connection header both tunnelling and device management services begin with.
    static void encode_connection_header(const span_uint8_t dest, const std::uint8_t channel_id, const std::uint8_t sequence_number,
                                         const std::uint8_t trailing) noexcept
    {
        dest[0] = connection_header_structure_length;
        dest[1] = channel_id;
        dest[2] = sequence_number;
        dest[3] = trailing;
    }

    expected_void_t encode_device_configuration_request_packet(const span_uint8_t dest, const std::uint8_t channel_id,
                                                               const std::uint8_t sequence_number, const cspan_uint8_t cemi_bytes) noexcept
    {
        if (channel_id == 0u)
            return std::unexpected(make_error_code(error::invalid_configuration));
        // One octet is enough: M_Reset.req is nothing but its message code.
        if (cemi_bytes.empty() || (cemi_bytes.size() > cemi_max_size))
            return std::unexpected(make_error_code(error::invalid_length));

        const auto total_length = communication_header_size + tunnelling_request_header_size + cemi_bytes.size();
        if (dest.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));

        const auto header =
            encode_communication_header(dest, device_configuration_request_service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        encode_connection_header({dest.data() + communication_header_size, tunnelling_request_header_size}, channel_id, sequence_number,
                                 0x00u);
        std::copy_n(cemi_bytes.begin(), cemi_bytes.size(), dest.begin() + communication_header_size + tunnelling_request_header_size);
        return {};
    }

    std::expected<device_configuration_frame, std::error_code> decode_device_configuration_request_packet(const cspan_uint8_t buf) noexcept
    {
        const auto header = decode_communication_header(buf);
        if (!header.has_value())
            return std::unexpected(header.error());
        if (header->service_type != device_configuration_request_service)
            return std::unexpected(make_error_code(error::unsupported_service));
        if (header->total_length != buf.size())
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto body = buf.subspan(communication_header_size);
        const auto connection = decode_connection_header(body, true);
        if (!connection.has_value())
            return std::unexpected(connection.error());

        const auto message_length = body.size() - tunnelling_request_header_size;
        if ((message_length == 0u) || (message_length > cemi_max_size))
            return std::unexpected(make_error_code(error::invalid_length));

        device_configuration_frame decoded {};
        decoded.channel_id = connection->channel_id;
        decoded.sequence_number = connection->sequence_number;
        decoded.message_length = static_cast<std::uint16_t>(message_length);
        decoded.cemi_bytes.size = static_cast<std::uint16_t>(message_length);
        std::copy_n(body.begin() + tunnelling_request_header_size, message_length, decoded.cemi_bytes.bytes.begin());
        return decoded;
    }

    expected_void_t encode_device_configuration_ack_packet(const span_uint8_t dest, const std::uint8_t channel_id,
                                                           const std::uint8_t sequence_number, const std::uint8_t status) noexcept
    {
        if (channel_id == 0u)
            return std::unexpected(make_error_code(error::invalid_configuration));
        constexpr auto total_length = communication_header_size + tunnelling_ack_size;
        if (dest.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));

        const auto header = encode_communication_header(dest, device_configuration_ack_service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        encode_connection_header({dest.data() + communication_header_size, tunnelling_ack_size}, channel_id, sequence_number, status);
        return {};
    }

    std::expected<tunnelling_ack_frame, std::error_code> decode_device_configuration_ack_packet(const cspan_uint8_t buf) noexcept
    {
        const auto header = decode_communication_header(buf);
        if (!header.has_value())
            return std::unexpected(header.error());
        if (header->service_type != device_configuration_ack_service)
            return std::unexpected(make_error_code(error::unsupported_service));
        if ((header->total_length != buf.size()) || (buf.size() != communication_header_size + tunnelling_ack_size))
            return std::unexpected(make_error_code(error::malformed_frame));

        return decode_connection_header(buf.subspan(communication_header_size), false);
    }

    /// @brief Indicates whether a feature identifier is one this build knows.
    [[nodiscard]] static constexpr bool valid_feature(const std::uint8_t value) noexcept
    {
        switch (static_cast<tunnelling_feature>(value))
        {
            case tunnelling_feature::supported_emi_type:
            case tunnelling_feature::host_device_descriptor:
            case tunnelling_feature::bus_connection_status:
            case tunnelling_feature::manufacturer_code:
            case tunnelling_feature::active_emi_type:
            case tunnelling_feature::individual_address:
            case tunnelling_feature::max_apdu_length:
            case tunnelling_feature::info_service_enable:
                return true;
        }

        return false;
    }

    /// @brief Indicates whether a tunnelling feature service carries a value, given its return code.
    /// @param service The service type.
    /// @param return_code The return code octet, which is meaningful only in a response.
    /// @return `true` when a value must be present.
    /// @details A get asks a question and carries nothing. A set and an unsolicited info both carry the
    ///          value they are about. A response carries one only when it succeeded: a failing response
    ///          reports the return code instead.
    [[nodiscard]] static constexpr bool feature_service_carries_value(const std::uint16_t service, const std::uint8_t return_code) noexcept
    {
        if ((service == tunnelling_feature_set_service) || (service == tunnelling_feature_info_service))
            return true;
        if (service == tunnelling_feature_response_service)
            return return_code == 0u;
        return false;
    }

    /// @brief Checks a tunnelling feature frame and the value beside it can be encoded together.
    /// @param value The frame naming the service, channel and feature.
    /// @param feature_value The value the service carries, which may be empty.
    /// @return Nothing, or why the pair cannot be encoded.
    [[nodiscard]] static expected_void_t validate_feature(const tunnelling_feature_frame& value, const cspan_uint8_t feature_value) noexcept
    {
        if (!is_tunnelling_feature_service(value.service_type))
            return std::unexpected(make_error_code(error::unsupported_service));
        if (value.channel_id == 0u)
            return std::unexpected(make_error_code(error::invalid_configuration));
        if (!valid_feature(static_cast<std::uint8_t>(value.feature)))
            return std::unexpected(make_error_code(error::invalid_configuration));
        if (feature_value.size() > tunnelling_feature_value::capacity)
            return std::unexpected(make_error_code(error::invalid_length));

        // A get that carries a value, or a set that carries none, is malformed rather than merely unusual:
        // the receiver has no way to read it as the service it claims to be.
        if (feature_service_carries_value(value.service_type, value.return_code) == feature_value.empty())
            return std::unexpected(make_error_code(error::malformed_frame));
        return {};
    }

    expected_void_t encode_tunnelling_feature_packet(const span_uint8_t dest, const tunnelling_feature_frame& value,
                                                     const cspan_uint8_t feature_value) noexcept
    {
        if (const auto valid = validate_feature(value, feature_value); !valid.has_value())
            return std::unexpected(valid.error());

        const auto total_length =
            communication_header_size + tunnelling_request_header_size + tunnelling_feature_header_size + feature_value.size();
        if (dest.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));

        const auto header = encode_communication_header(dest, value.service_type, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        encode_connection_header({dest.data() + communication_header_size, tunnelling_request_header_size}, value.channel_id,
                                 value.sequence_number, 0x00u);

        const auto body = communication_header_size + tunnelling_request_header_size;
        dest[body] = static_cast<std::uint8_t>(value.feature);
        // Reserved and zero in everything but a response, where it reports the outcome.
        dest[body + 1u] = (value.service_type == tunnelling_feature_response_service) ? value.return_code : std::uint8_t {};
        std::copy_n(feature_value.begin(), feature_value.size(), dest.begin() + body + tunnelling_feature_header_size);
        return {};
    }

    /// @brief Reads the fixed part of a tunnelling feature service: its connection header and the
    ///        feature it names.
    /// @param buf The whole datagram.
    /// @param header The already-decoded communication header.
    /// @return The frame with everything but its value filled in, or why the octets are not one.
    [[nodiscard]] static std::expected<tunnelling_feature_frame, std::error_code> decode_feature_prologue(
        const cspan_uint8_t buf, const communication_header& header) noexcept
    {
        const auto connection = decode_connection_header(buf.subspan(communication_header_size), true);
        if (!connection.has_value())
            return std::unexpected(connection.error());

        const auto body = communication_header_size + tunnelling_request_header_size;
        if (!valid_feature(buf[body]))
            return std::unexpected(make_error_code(error::unsupported_service));
        // Reserved and zero in everything but a response, where it reports the outcome.
        const auto is_response = header.service_type == tunnelling_feature_response_service;
        if (!is_response && (buf[body + 1u] != 0u))
            return std::unexpected(make_error_code(error::malformed_frame));

        tunnelling_feature_frame decoded {};
        decoded.service_type = header.service_type;
        decoded.channel_id = connection->channel_id;
        decoded.sequence_number = connection->sequence_number;
        decoded.feature = static_cast<tunnelling_feature>(buf[body]);
        decoded.return_code = is_response ? buf[body + 1u] : std::uint8_t {};
        return decoded;
    }

    /// @brief Reads the feature value a tunnelling feature service carries, if it carries one.
    /// @param buf The whole datagram.
    /// @param minimum The offset the value starts at.
    /// @param decoded The frame to fill in; its service type and return code select what is expected.
    /// @return Nothing, or why the value does not match the service that carries it.
    [[nodiscard]] static expected_void_t decode_feature_value(const cspan_uint8_t buf, const std::size_t minimum,
                                                              tunnelling_feature_frame& decoded) noexcept
    {
        const auto value_size = buf.size() - minimum;
        if (value_size > tunnelling_feature_value::capacity)
            return std::unexpected(make_error_code(error::invalid_length));
        if (feature_service_carries_value(decoded.service_type, decoded.return_code) != (value_size != 0u))
            return std::unexpected(make_error_code(error::malformed_frame));

        decoded.value.size = static_cast<std::uint8_t>(value_size);
        std::copy_n(buf.begin() + minimum, value_size, decoded.value.bytes.begin());
        return {};
    }

    std::expected<tunnelling_feature_frame, std::error_code> decode_tunnelling_feature_packet(const cspan_uint8_t buf) noexcept
    {
        const auto header = decode_communication_header(buf);
        if (!header.has_value())
            return std::unexpected(header.error());
        if (!is_tunnelling_feature_service(header->service_type))
            return std::unexpected(make_error_code(error::unsupported_service));
        if (header->total_length != buf.size())
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto minimum = communication_header_size + tunnelling_request_header_size + tunnelling_feature_header_size;
        if (buf.size() < minimum)
            return std::unexpected(make_error_code(error::malformed_frame));

        auto decoded = decode_feature_prologue(buf, header.value());
        if (!decoded.has_value())
            return std::unexpected(decoded.error());
        if (const auto value = decode_feature_value(buf, minimum, decoded.value()); !value.has_value())
            return std::unexpected(value.error());
        return decoded;
    }
}
