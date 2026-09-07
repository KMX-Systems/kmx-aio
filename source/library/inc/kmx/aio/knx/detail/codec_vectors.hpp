/// @file aio/knx/detail/codec_vectors.hpp
/// @brief Golden wire vectors for the pure KNX codec, checked at compile time.
/// @details
/// Every vector below is a byte sequence a real KNX installation puts on the wire. Including this header
/// runs the encoder and the decoder over each of them during translation, so a change that alters the wire
/// format stops the build instead of producing a library that talks to nothing.
///
/// This is what the `constexpr` codec and the @ref kmx::aio::knx::error enumeration buy: `std::error_code`
/// is not a literal type, so a codec that reported failures that way could not be exercised here at all.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstdint>
#endif

#include <kmx/aio/knx/address.hpp>
#include <kmx/aio/knx/cemi.hpp>
#include <kmx/aio/knx/dpt.hpp>

namespace kmx::aio::knx::detail::vectors
{
    /// @brief The sending device used by the vectors, 1.1.1.
    inline constexpr individual_address source {1u, 1u, 1u};
    /// @brief The destination group used by the vectors, 1/2/3.
    inline constexpr group_address destination = group_address::make(1u, 2u, 3u).value();

    /// @brief A_GroupValue_Write of the switch-on telegram to 1/2/3.
    inline constexpr std::array<std::uint8_t, 11u> group_value_write_on {
        0x11u, 0x00u, 0xBCu, 0xE0u, 0x11u, 0x01u, 0x0Au, 0x03u, 0x01u, 0x00u, 0x81u,
    };

    /// @brief A_GroupValue_Read addressed to 1/2/3.
    inline constexpr std::array<std::uint8_t, 11u> group_value_read {
        0x11u, 0x00u, 0xBCu, 0xE0u, 0x11u, 0x01u, 0x0Au, 0x03u, 0x01u, 0x00u, 0x00u,
    };

    /// @brief A_GroupValue_Write carrying 21.5 degrees as a 2-octet KNX float.
    inline constexpr std::array<std::uint8_t, 13u> group_value_write_temperature {
        0x11u, 0x00u, 0xBCu, 0xE0u, 0x11u, 0x01u, 0x0Au, 0x03u, 0x03u, 0x00u, 0x80u, 0x0Cu, 0x33u,
    };

    /// @brief An L_Data.ind reporting the switch-on telegram, five hops left.
    inline constexpr std::array<std::uint8_t, 11u> indication_switch_on {
        0x29u, 0x00u, 0xBCu, 0xD0u, 0x11u, 0x01u, 0x0Au, 0x03u, 0x01u, 0x00u, 0x81u,
    };

    /// @brief Encodes the switch-on telegram the way an application would.
    /// @return The encoded bytes, or an all-zero buffer when the encoding did not fill it exactly.
    [[nodiscard]] constexpr std::array<std::uint8_t, group_value_write_on.size()> encode_switch_on() noexcept
    {
        const auto value = dpt::traits<1u>::encode(true).value();
        std::array<std::uint8_t, group_value_write_on.size()> buffer {};
        const auto size = cemi::encode_group_value_write(buffer, destination, value.apdu(), source);
        return (size.has_value() && (*size == buffer.size())) ? buffer : decltype(buffer) {};
    }

    /// @brief Encodes the group read the way an application would.
    /// @return The encoded bytes, or an all-zero buffer when the encoding did not fill it exactly.
    [[nodiscard]] constexpr std::array<std::uint8_t, group_value_read.size()> encode_read() noexcept
    {
        std::array<std::uint8_t, group_value_read.size()> buffer {};
        const auto size = cemi::encode_group_value_read(buffer, destination, source);
        return (size.has_value() && (*size == buffer.size())) ? buffer : decltype(buffer) {};
    }

    /// @brief Encodes the temperature telegram the way an application would.
    /// @return The encoded bytes, or an all-zero buffer when the encoding did not fill it exactly.
    [[nodiscard]] constexpr std::array<std::uint8_t, group_value_write_temperature.size()> encode_temperature() noexcept
    {
        const auto value = dpt::traits<9u>::encode(21.5f).value();
        std::array<std::uint8_t, group_value_write_temperature.size()> buffer {};
        const auto size = cemi::encode_group_value_write(buffer, destination, value.apdu(), source);
        return (size.has_value() && (*size == buffer.size())) ? buffer : decltype(buffer) {};
    }

    static_assert(encode_switch_on() == group_value_write_on,
                  "A_GroupValue_Write of a switch-on telegram must match the captured wire bytes");
    static_assert(encode_read() == group_value_read, "A_GroupValue_Read must match the captured wire bytes");
    static_assert(encode_temperature() == group_value_write_temperature, "A 2-octet float group write must match the captured wire bytes");

    /// @brief Decodes a vector, which fails the build when the vector cannot be decoded at all.
    /// @param bytes The vector to decode.
    /// @return The decoded frame.
    [[nodiscard]] constexpr cemi_frame decoded(const cspan_uint8_t bytes) noexcept
    {
        return cemi::decode(bytes).value();
    }

    static_assert(decoded(group_value_write_on).message_code == cemi_message_code::l_data_req);
    static_assert(decoded(group_value_write_on).source == source);
    static_assert(decoded(group_value_write_on).group_addressed());
    static_assert(decoded(group_value_write_on).group_destination() == destination);
    static_assert(decoded(group_value_write_on).application_service == apci::group_value_write);
    static_assert(decoded(group_value_write_on).compact());
    static_assert(decoded(group_value_write_on).standard_frame());
    static_assert(decoded(group_value_write_on).telegram_priority() == priority::low);
    static_assert(decoded(group_value_write_on).hop_count() == 6u);
    static_assert(dpt::traits<1u>::decode(dpt::make_value_view(decoded(group_value_write_on), group_value_write_on)).value(),
                  "the switch-on vector must decode as a set bit");

    static_assert(decoded(group_value_read).application_service == apci::group_value_read);
    static_assert(decoded(group_value_read).compact_value == 0u);

    static_assert(decoded(group_value_write_temperature).data_length == 3u);
    static_assert(!decoded(group_value_write_temperature).compact());
    static_assert(decoded(group_value_write_temperature).payload_size == 2u);
    static_assert(dpt::traits<9u>::decode(dpt::make_value_view(decoded(group_value_write_temperature), group_value_write_temperature))
                          .value() == 21.5f,
                  "the temperature vector must decode back to the value it was built from");

    static_assert(decoded(indication_switch_on).message_code == cemi_message_code::l_data_ind);
    static_assert(decoded(indication_switch_on).hop_count() == 5u);
    static_assert(!decoded(indication_switch_on).repetition(), "the indication vector is not marked as a repetition");

    static_assert(individual_address::parse("1.1.1").value() == source, "individual address text must round-trip");
    static_assert(group_address::parse("1/2/3").value() == destination, "three-level group address text must round-trip");
    static_assert(group_address::parse("1/515").value() == destination, "two-level group address text names the same wire value");
    static_assert(group_address::parse("2563").value() == destination, "a free group address names the same wire value");
    static_assert(!individual_address::parse("1.1").has_value(), "an incomplete individual address must be rejected");
    static_assert(!individual_address::parse("16.1.1").has_value(), "an out-of-range area must be rejected");
    static_assert(!group_address::parse("1/2/3/4").has_value(), "a four-level group address must be rejected");

    static_assert(!cemi::decode(cspan_uint8_t {group_value_write_on.data(), group_value_write_on.size() - 1u}).has_value(),
                  "a truncated frame must be rejected");
    static_assert(cemi::decode(cspan_uint8_t {group_value_write_on.data(), 2u}).error() == error::malformed_frame,
                  "a frame cut short of its link header must be rejected as malformed");
}
