/// @file api/kmx/aio/knx/dpt/descriptor.hpp
/// @brief What a KNX datapoint main type looks like on the wire, and the table of every main type this build implements.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @reference KNX System Specifications, Volume 3/7/2 "Datapoint Types".
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/knx/error.hpp>

        #include <array>
        #include <cstdint>
        #include <expected>
        #include <string_view>
    #endif

namespace kmx::aio::knx::dpt
{
    /// @brief What a main type looks like on the wire.
    struct descriptor
    {
        /// @brief The main type.
        std::uint16_t main {};
        /// @brief The value width in bits.
        std::uint16_t bit_size {};
        /// @brief The name of the format, for logs and diagnostics.
        std::string_view name {};

        /// @brief Indicates whether the value rides in the six spare bits of the APCI octet.
        [[nodiscard]] constexpr bool compact() const noexcept { return bit_size <= 6u; }
        /// @brief Returns the number of payload octets a non-compact value occupies.
        [[nodiscard]] constexpr std::size_t octet_size() const noexcept { return compact() ? 0u : ((bit_size + 7u) / 8u); }
    };

    /// @brief Every main type this build implements, in one table.
    /// @note This is the single source of truth for widths and names. Every specialisation of
    ///       @ref kmx::aio::knx::dpt::traits takes its descriptor straight from this table through @ref describe, so
    ///       the two cannot drift apart, and a specialisation for a main type missing from the table fails to compile.
    inline constexpr std::array<descriptor, 19u> descriptors {{
        {1u, 1u, "1-bit"},
        {2u, 2u, "1-bit controlled"},
        {3u, 4u, "3-bit controlled"},
        {4u, 8u, "character"},
        {5u, 8u, "8-bit unsigned"},
        {6u, 8u, "8-bit signed"},
        {7u, 16u, "2-octet unsigned"},
        {8u, 16u, "2-octet signed"},
        {9u, 16u, "2-octet float"},
        {10u, 24u, "time of day"},
        {11u, 24u, "date"},
        {12u, 32u, "4-octet unsigned"},
        {13u, 32u, "4-octet signed"},
        {14u, 32u, "4-octet float"},
        {16u, 112u, "character string"},
        {17u, 8u, "scene number"},
        {18u, 8u, "scene control"},
        {20u, 8u, "1-octet enumeration"},
        {232u, 24u, "RGB colour"},
    }};

    /// @brief Looks a main type up in @ref descriptors.
    /// @param main The main type to look up.
    /// @return The descriptor, or `error::unsupported_datapoint` when the main type is not implemented.
    [[nodiscard]] constexpr std::expected<descriptor, error> describe(const std::uint16_t main) noexcept
    {
        for (const auto& entry: descriptors)
            if (entry.main == main)
                return entry;

        return std::unexpected(error::unsupported_datapoint);
    }
}
#endif // KMX_AIO_FEATURE_KNX
