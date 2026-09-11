/// @file api/kmx/aio/knx/tunnelling_feature_value.hpp
/// @brief Inline storage for the value a KNXnet/IP tunnelling feature service carries.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @reference KNX System Specifications, 03/08/04 "Tunnelling", tunnelling feature identifiers.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>

        #include <array>
        #include <cstdint>
    #endif

namespace kmx::aio::knx
{
    /// @brief Inline storage for a tunnelling feature value.
    /// @details Every defined feature value is one or two octets; the capacity here is generous for them
    ///          and is a hard bound rather than an assumption, so a peer cannot make the decoder write past
    ///          it. A longer value is reported as `error::invalid_length` instead of being truncated.
    struct tunnelling_feature_value
    {
        /// @brief Largest feature value this build accepts.
        static constexpr std::size_t capacity = 16u;

        /// @brief The storage; only its first @ref size octets are meaningful.
        std::array<std::uint8_t, capacity> bytes {};
        /// @brief How many octets of @ref bytes the value occupies.
        std::uint8_t size {};

        /// @brief Indicates whether the value is absent, as it is in a get request.
        [[nodiscard]] constexpr bool empty() const noexcept { return size == 0u; }
        /// @brief Returns how many octets the value occupies.
        [[nodiscard]] constexpr std::size_t length() const noexcept { return size; }
        /// @brief Returns an iterator to the first octet.
        [[nodiscard]] constexpr auto begin() const noexcept { return bytes.begin(); }
        /// @brief Returns an iterator one past the last meaningful octet.
        [[nodiscard]] constexpr auto end() const noexcept { return bytes.begin() + size; }
        /// @brief Returns a view of the meaningful octets.
        [[nodiscard]] constexpr cspan_uint8_t span() const noexcept { return {bytes.data(), size}; }
    };
}
#endif // KMX_AIO_FEATURE_KNX
