/// @file api/kmx/aio/knx/dpt/value_view.hpp
/// @brief A read-only view of a received KNX datapoint value, and how to take one from a decoded cEMI frame.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @reference KNX System Specifications, Volume 3/7/2 "Datapoint Types".
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/cemi_frame.hpp>

        #include <cstdint>
        #include <span>
    #endif

namespace kmx::aio::knx::dpt
{
    /// @brief A read-only view of a received datapoint value.
    struct value_view
    {
        /// @brief Whether the value rode in the six spare bits of the APCI octet.
        bool compacted {true};
        /// @brief The six-bit value; meaningful only when @ref compacted.
        std::uint8_t compact_value {};
        /// @brief The value octets; empty when @ref compacted.
        cspan_uint8_t octets {};

        /// @brief Returns the number of value octets, which is zero for a compact value.
        [[nodiscard]] constexpr std::size_t size() const noexcept { return octets.size(); }
    };

    /// @brief Builds a value view from a decoded cEMI frame.
    /// @param frame The decoded frame.
    /// @param bytes The very buffer the frame was decoded from.
    /// @return A view of the frame's application value.
    [[nodiscard]] constexpr value_view make_value_view(const cemi_frame& frame, const cspan_uint8_t bytes) noexcept
    {
        if (frame.compact())
            return {.compacted = true, .compact_value = frame.compact_value, .octets = {}};

        return {.compacted = false, .compact_value = 0u, .octets = frame.payload(bytes)};
    }
}
#endif // KMX_AIO_FEATURE_KNX
