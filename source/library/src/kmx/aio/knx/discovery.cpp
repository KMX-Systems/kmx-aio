/// @file src/kmx/aio/knx/discovery.cpp
/// @brief KNXnet/IP SEARCH, extended SEARCH and DESCRIPTION request and response codecs.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/discovery.hpp>
#ifndef PCH
    #include <kmx/aio/knx/dib.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/frame.hpp>

    #include <algorithm>
    #include <cstddef>
    #include <optional>
#endif

namespace kmx::aio::knx::discovery
{
    // One definition of what a well-formed run of description blocks is, in the module that models them.
    // Two copies of this walk would be two chances for a decoder and a validator to disagree about which
    // datagrams are acceptable.
    [[nodiscard]] static bool valid_dibs(const cspan_uint8_t dibs) noexcept
    {
        return dib::valid_blocks(dibs);
    }

    [[nodiscard]] static std::expected<hpai, std::error_code> decode_hpai(const cspan_uint8_t source) noexcept
    {
        if (!source.empty() && (source[0] == 20u))
            return std::unexpected(make_error_code(error::unsupported_hpai));
        if ((source.size() < connection::hpai_size) || (source[0] != connection::hpai_size))
            return std::unexpected(make_error_code(error::malformed_frame));
        if (source[1] != 0x01u)
            return std::unexpected(make_error_code(error::unsupported_hpai));

        hpai value {};
        value.protocol = source[1];
        value.endpoint.address = {source[2], source[3], source[4], source[5]};
        value.endpoint.port =
            static_cast<std::uint16_t>((static_cast<std::uint16_t>(source[6]) << 8u) | static_cast<std::uint16_t>(source[7]));
        return value;
    }

    static void encode_hpai(const span_uint8_t dest, const hpai& value) noexcept
    {
        dest[0] = static_cast<std::uint8_t>(connection::hpai_size);
        dest[1] = value.protocol;
        dest[2] = value.endpoint.address[0];
        dest[3] = value.endpoint.address[1];
        dest[4] = value.endpoint.address[2];
        dest[5] = value.endpoint.address[3];
        dest[6] = static_cast<std::uint8_t>((value.endpoint.port >> 8u) & 0xFFu);
        dest[7] = static_cast<std::uint8_t>(value.endpoint.port & 0xFFu);
    }

    static void encode_ipv6_hpai(const span_uint8_t dest, const ipv6_hpai& value) noexcept
    {
        dest[0] = static_cast<std::uint8_t>(connection::ipv6_hpai_size);
        dest[1] = value.protocol;
        std::copy_n(value.endpoint.address.begin(), value.endpoint.address.size(), dest.begin() + 2u);
        dest[18u] = static_cast<std::uint8_t>(value.endpoint.port >> 8u);
        dest[19u] = static_cast<std::uint8_t>(value.endpoint.port & 0xFFu);
    }

    [[nodiscard]] static std::expected<ipv6_hpai, std::error_code> decode_ipv6_hpai(const cspan_uint8_t source) noexcept
    {
        if ((source.size() < connection::ipv6_hpai_size) || (source[0] != connection::ipv6_hpai_size))
            return std::unexpected(make_error_code(error::malformed_frame));
        if (source[1] != 0x01u)
            return std::unexpected(make_error_code(error::unsupported_hpai));
        ipv6_hpai value {};
        value.protocol = source[1];
        std::copy_n(source.begin() + 2u, value.endpoint.address.size(), value.endpoint.address.begin());
        value.endpoint.port = static_cast<std::uint16_t>((static_cast<std::uint16_t>(source[18u]) << 8u) | source[19u]);
        return value;
    }

    expected_void_t encode_search_request_packet(const span_uint8_t dest, const search_request_frame& request) noexcept
    {
        const auto total_length = frame::communication_header_size + search_request_body_size;
        if (dest.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));
        if (request.discovery_endpoint.protocol != 0x01u)
            return std::unexpected(make_error_code(error::unsupported_hpai));

        const auto header = frame::encode_communication_header(dest, search_request_service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        encode_hpai({dest.data() + frame::communication_header_size, connection::hpai_size}, request.discovery_endpoint);
        return {};
    }

    std::expected<search_request_frame, std::error_code> decode_search_request_packet(const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->protocol_version != 0x10u) || (header->service_type != search_request_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        if ((header->total_length != packet.size()) || (packet.size() != frame::communication_header_size + search_request_body_size))
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto endpoint = decode_hpai({packet.data() + frame::communication_header_size, connection::hpai_size});
        if (!endpoint.has_value())
            return std::unexpected(endpoint.error());

        return search_request_frame {endpoint.value()};
    }

    expected_void_t encode_ipv6_search_request_packet(const span_uint8_t dest, const ipv6_search_request_frame& request) noexcept
    {
        constexpr auto total_length = frame::communication_header_size + ipv6_search_request_body_size;
        if (dest.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));
        if (request.discovery_endpoint.protocol != 0x01u)
            return std::unexpected(make_error_code(error::unsupported_hpai));
        const auto header = frame::encode_communication_header(dest, search_request_service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());
        encode_ipv6_hpai({dest.data() + frame::communication_header_size, connection::ipv6_hpai_size}, request.discovery_endpoint);
        return {};
    }

    std::expected<ipv6_search_request_frame, std::error_code> decode_ipv6_search_request_packet(const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->protocol_version != 0x10u) || (header->service_type != search_request_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        if ((header->total_length != packet.size()) || (packet.size() != frame::communication_header_size + ipv6_search_request_body_size))
            return std::unexpected(make_error_code(error::malformed_frame));
        const auto endpoint = decode_ipv6_hpai({packet.data() + frame::communication_header_size, connection::ipv6_hpai_size});
        if (!endpoint.has_value())
            return std::unexpected(endpoint.error());
        return ipv6_search_request_frame {endpoint.value()};
    }

    expected_void_t encode_search_response_packet(const span_uint8_t dest, const search_response_frame& response) noexcept
    {
        constexpr std::size_t fixed_body_size = connection::hpai_size;
        const auto total_length = frame::communication_header_size + fixed_body_size + response.device_info_blocks.size();
        if ((total_length > frame::max_total_length) || (dest.size() < total_length))
            return std::unexpected(make_error_code(error::invalid_length));
        if (response.control_endpoint.protocol != 0x01u)
            return std::unexpected(make_error_code(error::unsupported_hpai));
        if (!valid_dibs(response.device_info_blocks))
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto header = frame::encode_communication_header(dest, search_response_service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        encode_hpai({dest.data() + frame::communication_header_size, connection::hpai_size}, response.control_endpoint);
        std::copy_n(response.device_info_blocks.begin(), response.device_info_blocks.size(),
                    dest.begin() + frame::communication_header_size + connection::hpai_size);
        return {};
    }

    std::expected<search_response_frame, std::error_code> decode_search_response_packet(const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->protocol_version != 0x10u) || (header->service_type != search_response_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        if ((header->total_length != packet.size()) || (packet.size() < frame::communication_header_size + connection::hpai_size + 2u))
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto endpoint = decode_hpai({packet.data() + frame::communication_header_size, connection::hpai_size});
        if (!endpoint.has_value())
            return std::unexpected(endpoint.error());

        const auto dib_offset = frame::communication_header_size + connection::hpai_size;
        const auto dib_size = packet.size() - dib_offset;
        if (!valid_dibs({packet.data() + dib_offset, dib_size}))
            return std::unexpected(make_error_code(error::malformed_frame));

        search_response_frame response {endpoint.value(), {}};
        response.device_info_blocks.assign(packet.begin() + dib_offset, packet.end());
        return response;
    }

    expected_void_t encode_ipv6_search_response_packet(const span_uint8_t dest, const ipv6_search_response_frame& response) noexcept
    {
        const auto total_length = frame::communication_header_size + connection::ipv6_hpai_size + response.device_info_blocks.size();
        if ((total_length > frame::max_total_length) || (dest.size() < total_length))
            return std::unexpected(make_error_code(error::invalid_length));
        if (response.control_endpoint.protocol != 0x01u)
            return std::unexpected(make_error_code(error::unsupported_hpai));
        if (!valid_dibs(response.device_info_blocks))
            return std::unexpected(make_error_code(error::malformed_frame));
        const auto header = frame::encode_communication_header(dest, search_response_service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());
        encode_ipv6_hpai({dest.data() + frame::communication_header_size, connection::ipv6_hpai_size}, response.control_endpoint);
        std::copy_n(response.device_info_blocks.begin(), response.device_info_blocks.size(),
                    dest.begin() + frame::communication_header_size + connection::ipv6_hpai_size);
        return {};
    }

    std::expected<ipv6_search_response_frame, std::error_code> decode_ipv6_search_response_packet(const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->protocol_version != 0x10u) || (header->service_type != search_response_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        const auto dib_offset = frame::communication_header_size + connection::ipv6_hpai_size;
        if ((header->total_length != packet.size()) || (packet.size() < dib_offset + 2u))
            return std::unexpected(make_error_code(error::malformed_frame));
        const auto endpoint = decode_ipv6_hpai({packet.data() + frame::communication_header_size, connection::ipv6_hpai_size});
        if (!endpoint.has_value())
            return std::unexpected(endpoint.error());
        const auto dib_size = packet.size() - dib_offset;
        if (!valid_dibs({packet.data() + dib_offset, dib_size}))
            return std::unexpected(make_error_code(error::malformed_frame));
        ipv6_search_response_frame response {endpoint.value(), {}};
        response.device_info_blocks.assign(packet.begin() + dib_offset, packet.end());
        return response;
    }

    expected_void_t encode_description_request_packet(const span_uint8_t dest, const description_request_frame& request) noexcept
    {
        constexpr auto total_length = frame::communication_header_size + description_request_body_size;
        if (dest.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));
        if (request.control_endpoint.protocol != 0x01u)
            return std::unexpected(make_error_code(error::unsupported_hpai));

        const auto header = frame::encode_communication_header(dest, description_request_service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        encode_hpai({dest.data() + frame::communication_header_size, connection::hpai_size}, request.control_endpoint);
        return {};
    }

    std::expected<description_request_frame, std::error_code> decode_description_request_packet(const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->protocol_version != 0x10u) || (header->service_type != description_request_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        constexpr auto total_length = frame::communication_header_size + description_request_body_size;
        if ((header->total_length != packet.size()) || (packet.size() != total_length))
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto endpoint = decode_hpai({packet.data() + frame::communication_header_size, connection::hpai_size});
        if (!endpoint.has_value())
            return std::unexpected(endpoint.error());

        return description_request_frame {endpoint.value()};
    }

    expected_void_t encode_description_response_packet(const span_uint8_t dest, const description_response_frame& response) noexcept
    {
        const auto total_length = frame::communication_header_size + response.device_info_blocks.size();
        if ((total_length > frame::max_total_length) || (dest.size() < total_length))
            return std::unexpected(make_error_code(error::invalid_length));
        if (!valid_dibs(response.device_info_blocks))
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto header =
            frame::encode_communication_header(dest, description_response_service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());
        std::copy_n(response.device_info_blocks.begin(), response.device_info_blocks.size(),
                    dest.begin() + frame::communication_header_size);
        return {};
    }

    std::expected<description_response_frame, std::error_code> decode_description_response_packet(const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->protocol_version != 0x10u) || (header->service_type != description_response_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        if ((header->total_length != packet.size()) || (packet.size() < frame::communication_header_size + 2u))
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto dib_size = packet.size() - frame::communication_header_size;
        const auto dib_offset = frame::communication_header_size;
        if (!valid_dibs({packet.data() + dib_offset, dib_size}))
            return std::unexpected(make_error_code(error::malformed_frame));

        description_response_frame response {};
        response.device_info_blocks.assign(packet.begin() + dib_offset, packet.end());
        return response;
    }

    /// @brief Returns the fixed data size a search request parameter type requires, if it has one.
    /// @param type The parameter type.
    /// @return The required data size, or nothing when the type's data is variable.
    [[nodiscard]] static constexpr std::optional<std::size_t> search_parameter_data_size(const search_parameter_type type) noexcept
    {
        switch (type)
        {
            case search_parameter_type::programming_mode:
                return std::size_t {0u};
            case search_parameter_type::mac_address:
                return std::size_t {6u};
            case search_parameter_type::service:
                return std::size_t {2u};
            case search_parameter_type::request_dibs:
                return std::nullopt; // a list of DIB type codes, padded to an even length
        }

        return std::nullopt;
    }

    [[nodiscard]] static constexpr bool known_search_parameter(const std::uint8_t type) noexcept
    {
        switch (static_cast<search_parameter_type>(type))
        {
            case search_parameter_type::programming_mode:
            case search_parameter_type::mac_address:
            case search_parameter_type::service:
            case search_parameter_type::request_dibs:
                return true;
        }

        return false;
    }

    /// @brief Returns the encoded size of one parameter block, padding the variable-length one to even.
    [[nodiscard]] static std::expected<std::size_t, std::error_code> parameter_block_size(const search_parameter& value) noexcept
    {
        if (!known_search_parameter(static_cast<std::uint8_t>(value.type)))
            return std::unexpected(make_error_code(error::invalid_configuration));

        const auto required = search_parameter_data_size(value.type);
        if (required.has_value() && (value.data.size() != *required))
            return std::unexpected(make_error_code(error::invalid_configuration));

        auto size = search_parameter_header_size + value.data.size();
        // A parameter block is an even number of octets; the DIB list is the only one that can be odd.
        size += (size % 2u);
        if (size > 0xFFu)
            return std::unexpected(make_error_code(error::invalid_length));
        return size;
    }

    std::expected<std::size_t, std::error_code> extended_search_request_size(const extended_search_request_frame& request) noexcept
    {
        auto total = frame::communication_header_size + search_request_body_size;
        for (const auto& parameter: request.parameters)
        {
            const auto size = parameter_block_size(parameter);
            if (!size.has_value())
                return std::unexpected(size.error());
            total += *size;
        }

        if (total > frame::max_total_length)
            return std::unexpected(make_error_code(error::invalid_length));
        return total;
    }

    /// @brief Writes one search parameter block.
    /// @param dest The destination octets.
    /// @param offset Where in @p dest the block starts.
    /// @param parameter The parameter to write.
    /// @return How many octets the block took, or why it could not be written.
    [[nodiscard]] static std::expected<std::size_t, std::error_code> write_search_parameter(const span_uint8_t dest,
                                                                                            const std::size_t offset,
                                                                                            const search_parameter& parameter) noexcept
    {
        // Checked rather than dereferenced blind. It cannot fail here, because the size pass rejected
        // every parameter that would have made it fail - but that is an invariant held in another
        // function, and an encoder is the wrong place to depend on one.
        const auto size = parameter_block_size(parameter);
        if (!size.has_value())
            return std::unexpected(size.error());

        const auto data_offset = offset + search_parameter_header_size;
        dest[offset] = static_cast<std::uint8_t>(*size);
        dest[offset + 1u] = static_cast<std::uint8_t>((parameter.mandatory ? search_parameter_mandatory_mask : 0u) |
                                                      static_cast<std::uint8_t>(parameter.type));
        std::copy_n(parameter.data.begin(), parameter.data.size(), dest.begin() + data_offset);
        // The pad octet, when the data made the block odd, is written as zero.
        std::fill(dest.begin() + data_offset + parameter.data.size(), dest.begin() + offset + *size, std::uint8_t {});
        return size;
    }

    expected_void_t encode_extended_search_request_packet(const span_uint8_t dest, const extended_search_request_frame& request) noexcept
    {
        const auto total_length = extended_search_request_size(request);
        if (!total_length.has_value())
            return std::unexpected(total_length.error());
        if (dest.size() < *total_length)
            return std::unexpected(make_error_code(error::invalid_length));
        if (request.discovery_endpoint.protocol != 0x01u)
            return std::unexpected(make_error_code(error::unsupported_hpai));

        const auto header =
            frame::encode_communication_header(dest, search_request_extended_service, static_cast<std::uint16_t>(*total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        encode_hpai({dest.data() + frame::communication_header_size, connection::hpai_size}, request.discovery_endpoint);

        auto offset = frame::communication_header_size + search_request_body_size;
        for (const auto& parameter: request.parameters)
        {
            const auto written = write_search_parameter(dest, offset, parameter);
            if (!written.has_value())
                return std::unexpected(written.error());
            offset += *written;
        }

        return {};
    }

    /// @brief Checks a datagram is a SEARCH_REQUEST_EXTENDED long enough to hold its fixed part.
    /// @param packet The received datagram, header included.
    /// @return Nothing, or why the datagram is not one this decoder reads.
    [[nodiscard]] static expected_void_t validate_extended_search_header(const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->protocol_version != 0x10u) || (header->service_type != search_request_extended_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        if (header->total_length != packet.size())
            return std::unexpected(make_error_code(error::malformed_frame));
        if (packet.size() < frame::communication_header_size + search_request_body_size)
            return std::unexpected(make_error_code(error::malformed_frame));
        return {};
    }

    /// @brief A decoded parameter, nothing when the block is an unknown optional one to skip.
    using optional_search_parameter_result_t = std::expected<std::optional<search_parameter>, std::error_code>;

    /// @brief Decodes one search parameter block.
    /// @param block The block's octets, its own two-octet prologue included.
    /// @return The parameter, nothing when it is an unknown one the searcher did not insist on, or the
    ///         reason the block cannot be read.
    [[nodiscard]] static optional_search_parameter_result_t decode_search_parameter(const cspan_uint8_t block) noexcept
    {
        const auto type_octet = block[1u];
        const auto type = static_cast<std::uint8_t>(type_octet & search_parameter_type_mask);
        const auto mandatory = (type_octet & search_parameter_mandatory_mask) != 0u;
        if (!known_search_parameter(type))
        {
            // A parameter this build has no name for is skipped, unless the searcher marked it mandatory -
            // in which case answering without honouring it would be answering a different search.
            if (mandatory)
                return std::unexpected(make_error_code(error::unsupported_service));
            return std::optional<search_parameter> {};
        }

        search_parameter parameter {};
        parameter.mandatory = mandatory;
        parameter.type = static_cast<search_parameter_type>(type);

        const auto required = search_parameter_data_size(parameter.type);
        // The fixed-size parameters state their own size twice - in the block length and in the
        // specification - and a block whose length disagrees is not the parameter it claims to be.
        if (required.has_value() && ((block.size() - search_parameter_header_size) != *required))
            return std::unexpected(make_error_code(error::malformed_frame));

        parameter.data.assign(block.begin() + search_parameter_header_size, block.end());
        return std::optional<search_parameter> {std::move(parameter)};
    }

    std::expected<extended_search_request_frame, std::error_code> decode_extended_search_request_packet(const cspan_uint8_t packet) noexcept
    {
        if (const auto valid = validate_extended_search_header(packet); !valid.has_value())
            return std::unexpected(valid.error());

        const auto endpoint = decode_hpai({packet.data() + frame::communication_header_size, connection::hpai_size});
        if (!endpoint.has_value())
            return std::unexpected(endpoint.error());

        extended_search_request_frame decoded {};
        decoded.discovery_endpoint = endpoint.value();

        auto offset = frame::communication_header_size + search_request_body_size;
        while (offset < packet.size())
        {
            const std::size_t size = packet[offset];
            // A block that does not advance, or that runs past the datagram, would loop or read out of
            // bounds; both are rejected rather than salvaged.
            if ((size < search_parameter_header_size) || ((offset + size) > packet.size()))
                return std::unexpected(make_error_code(error::malformed_frame));

            auto parameter = decode_search_parameter(packet.subspan(offset, size));
            if (!parameter.has_value())
                return std::unexpected(parameter.error());
            if (parameter->has_value())
                decoded.parameters.push_back(std::move(**parameter));
            offset += size;
        }

        return decoded;
    }

    expected_void_t encode_extended_search_response_packet(const span_uint8_t dest, const extended_search_response_frame& response) noexcept
    {
        const auto total_length = frame::communication_header_size + connection::hpai_size + response.device_info_blocks.size();
        if ((total_length > frame::max_total_length) || (dest.size() < total_length))
            return std::unexpected(make_error_code(error::invalid_length));
        if (response.control_endpoint.protocol != 0x01u)
            return std::unexpected(make_error_code(error::unsupported_hpai));
        if (!valid_dibs(response.device_info_blocks))
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto header =
            frame::encode_communication_header(dest, search_response_extended_service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        encode_hpai({dest.data() + frame::communication_header_size, connection::hpai_size}, response.control_endpoint);
        std::copy_n(response.device_info_blocks.begin(), response.device_info_blocks.size(),
                    dest.begin() + frame::communication_header_size + connection::hpai_size);
        return {};
    }

    std::expected<extended_search_response_frame, std::error_code> decode_extended_search_response_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->protocol_version != 0x10u) || (header->service_type != search_response_extended_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        if (header->total_length != packet.size())
            return std::unexpected(make_error_code(error::malformed_frame));
        if (packet.size() < frame::communication_header_size + connection::hpai_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto endpoint = decode_hpai({packet.data() + frame::communication_header_size, connection::hpai_size});
        if (!endpoint.has_value())
            return std::unexpected(endpoint.error());

        const auto dib_offset = frame::communication_header_size + connection::hpai_size;
        if (!valid_dibs(packet.subspan(dib_offset)))
            return std::unexpected(make_error_code(error::malformed_frame));

        extended_search_response_frame response {endpoint.value(), {}};
        response.device_info_blocks.assign(packet.begin() + dib_offset, packet.end());
        return response;
    }
}
