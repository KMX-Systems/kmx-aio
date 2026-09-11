/// @file api/kmx/aio/knx/dpt.hpp
/// @brief KNX datapoint type encoding and decoding.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// A KNX group telegram carries a bare value whose meaning comes entirely from the datapoint type the
/// installation assigned to the group address. This header turns that value between its wire form and an
/// ordinary C++ type for the datapoint main types that real installations actually use.
///
/// Everything here is `constexpr` and allocation-free.
/// @reference KNX System Specifications, Volume 3/7/2 "Datapoint Types".
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/cemi_frame.hpp>
        #include <kmx/aio/knx/dpt/traits.hpp>
        #include <kmx/aio/knx/dpt/value_view.hpp>
        #include <kmx/aio/knx/error.hpp>

        #include <compare>
        #include <cstdint>
        #include <expected>
        #include <span>
    #endif

namespace kmx::aio::knx::dpt
{
    /// @brief A datapoint type identifier, written `main.sub`.
    struct id
    {
        /// @brief The main type, which fixes the wire format.
        std::uint16_t main {};
        /// @brief The sub type, which fixes the unit and range.
        std::uint16_t sub {};

        /// @brief Orders identifiers by main type and then sub type.
        [[nodiscard]] constexpr auto operator<=>(const id&) const noexcept = default;
    };

    /// @brief Encodes a value of the named datapoint main type.
    /// @tparam Main The datapoint main type.
    /// @param value The value to encode.
    /// @return The encoded payload, or the reason it could not be encoded.
    template <std::uint16_t Main>
    [[nodiscard]] constexpr encode_result_t encode(const value_t<Main>& value) noexcept
    {
        return traits<Main>::encode(value);
    }

    /// @brief Decodes a value of the named datapoint main type.
    /// @tparam Main The datapoint main type.
    /// @param value The received value.
    /// @return The decoded value, or the reason it could not be decoded.
    template <std::uint16_t Main>
    [[nodiscard]] constexpr decode_result_t<Main> decode(const value_view& value) noexcept
    {
        return traits<Main>::decode(value);
    }

    /// @brief Decodes a value of the named datapoint main type straight out of a received frame.
    /// @tparam Main The datapoint main type.
    /// @param frame The decoded cEMI frame.
    /// @param bytes The very buffer the frame was decoded from.
    /// @return The decoded value, or the reason it could not be decoded.
    template <std::uint16_t Main>
    [[nodiscard]] constexpr decode_result_t<Main> decode(const cemi_frame& frame, const cspan_uint8_t bytes) noexcept
    {
        return traits<Main>::decode(make_value_view(frame, bytes));
    }

    /// @brief Encodes a percentage as DPT 5.001, which scales 0..100% onto 0..255.
    /// @param percent The percentage, 0..100.
    /// @return The encoded payload, or `error::value_out_of_range` when the percentage exceeds 100.
    [[nodiscard]] constexpr encode_result_t encode_scaling(const double percent) noexcept
    {
        if (!(percent >= 0.0) || !(percent <= 100.0))
            return std::unexpected(error::value_out_of_range);

        return traits<5u>::encode(static_cast<std::uint8_t>(detail::round_to_int((percent * 255.0) / 100.0)));
    }

    /// @brief Decodes DPT 5.001 into a percentage.
    /// @param value The received value.
    /// @return The percentage, or the reason it could not be decoded.
    [[nodiscard]] constexpr std::expected<double, error> decode_scaling(const value_view& value) noexcept
    {
        const auto raw = traits<5u>::decode(value);
        if (!raw.has_value())
            return std::unexpected(raw.error());

        return (static_cast<double>(*raw) * 100.0) / 255.0;
    }

    /// @brief Encodes an angle as DPT 5.003, which scales 0..360 degrees onto 0..255.
    /// @param degrees The angle, 0..360.
    /// @return The encoded payload, or `error::value_out_of_range` when the angle is outside the circle.
    [[nodiscard]] constexpr encode_result_t encode_angle(const double degrees) noexcept
    {
        if (!(degrees >= 0.0) || !(degrees <= 360.0))
            return std::unexpected(error::value_out_of_range);

        return traits<5u>::encode(static_cast<std::uint8_t>(detail::round_to_int((degrees * 255.0) / 360.0)));
    }

    /// @brief Decodes DPT 5.003 into an angle in degrees.
    /// @param value The received value.
    /// @return The angle, or the reason it could not be decoded.
    [[nodiscard]] constexpr std::expected<double, error> decode_angle(const value_view& value) noexcept
    {
        const auto raw = traits<5u>::decode(value);
        if (!raw.has_value())
            return std::unexpected(raw.error());

        return (static_cast<double>(*raw) * 360.0) / 255.0;
    }
}
#endif // KMX_AIO_FEATURE_KNX
