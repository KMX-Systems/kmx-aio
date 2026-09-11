/// @file src/kmx/aio/http3/settings_codec.cpp
/// @brief HTTP/3 SETTINGS payload and frame encoding and decoding.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/http3/settings_codec.hpp>
#ifndef PCH
    #include <kmx/aio/http3/detail/varint.hpp>
    #include <kmx/aio/http3/frame.hpp>
    #include <kmx/aio/http3/frame_codec.hpp>

    #include <cstddef>
#endif

namespace kmx::aio::http3
{
    std::vector<std::uint8_t> settings_codec::encode(const settings& value) noexcept(false)
    {
        std::vector<std::uint8_t> payload;
        payload.reserve(32u);

        detail::encode_varint(payload, static_cast<std::uint64_t>(settings_identifier::qpack_max_table_capacity));
        detail::encode_varint(payload, value.qpack_max_table_capacity);

        detail::encode_varint(payload, static_cast<std::uint64_t>(settings_identifier::max_field_section_size));
        detail::encode_varint(payload, value.max_field_section_size);

        detail::encode_varint(payload, static_cast<std::uint64_t>(settings_identifier::qpack_blocked_streams));
        detail::encode_varint(payload, value.qpack_blocked_streams);

        if (value.enable_connect_protocol)
        {
            detail::encode_varint(payload, static_cast<std::uint64_t>(settings_identifier::enable_connect_protocol));
            detail::encode_varint(payload, 1u);
        }

        if (value.h3_datagram)
        {
            detail::encode_varint(payload, static_cast<std::uint64_t>(settings_identifier::h3_datagram));
            detail::encode_varint(payload, 1u);
        }

        return payload;
    }

    std::vector<std::uint8_t> settings_codec::encode_frame(const settings& value) noexcept(false)
    {
        const auto payload = encode(value);
        return frame_codec::encode(frame_type::settings, payload);
    }

    /// @brief Records one setting, ignoring identifiers this build has no field for.
    /// @param parsed The settings being built.
    /// @param identifier The setting's identifier.
    /// @param value The setting's value.
    /// @note An unknown identifier is skipped rather than refused: a peer is allowed to send settings a
    ///       given implementation does not know, and rejecting the connection over one would be wrong.
    static void apply_setting(settings& parsed, const settings_identifier identifier, const std::uint64_t value) noexcept
    {
        switch (identifier)
        {
            case settings_identifier::qpack_max_table_capacity:
                parsed.qpack_max_table_capacity = value;
                break;
            case settings_identifier::max_field_section_size:
                parsed.max_field_section_size = value;
                break;
            case settings_identifier::qpack_blocked_streams:
                parsed.qpack_blocked_streams = value;
                break;
            case settings_identifier::enable_connect_protocol:
                parsed.enable_connect_protocol = value != 0u;
                break;
            case settings_identifier::h3_datagram:
                parsed.h3_datagram = value != 0u;
                break;
            default:
                break;
        }
    }

    std::expected<settings, std::error_code> settings_codec::decode(cspan_uint8_t payload) noexcept
    {
        settings parsed {};
        std::size_t offset {};
        while (offset < payload.size())
        {
            auto identifier = detail::decode_varint(payload, offset);
            if (!identifier)
                return std::unexpected(make_error_code(error_code::settings_error));
            offset += identifier->second;

            auto value = detail::decode_varint(payload, offset);
            if (!value)
                return std::unexpected(make_error_code(error_code::settings_error));
            offset += value->second;

            apply_setting(parsed, static_cast<settings_identifier>(identifier->first), value->first);
        }

        return parsed;
    }

    std::expected<settings, std::error_code> settings_codec::decode_frame(cspan_uint8_t payload) noexcept
    {
        auto decoded = frame_codec::decode(payload);
        if (!decoded)
            return std::unexpected(decoded.error());
        if (decoded->type != frame_type::settings)
            return std::unexpected(make_error_code(error_code::frame_unexpected));
        return decode(decoded->payload);
    }
}
