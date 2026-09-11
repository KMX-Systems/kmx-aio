/// @file api/kmx/aio/knx/dpt/string_value.hpp
/// @brief The fixed-width character string of KNX datapoint main type 16.
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
    /// @brief A fixed-width character string, for main type 16.
    /// @note The wire format is fourteen octets padded with zeroes; a shorter string is not shorter on the bus.
    struct string_value
    {
        /// @brief The number of characters the wire format holds.
        static constexpr std::size_t capacity = 14u;

        /// @brief The characters, zero padded.
        std::array<char, capacity> data {};

        /// @brief Builds a string value from text.
        /// @param text The text to carry; must not exceed @ref capacity characters.
        /// @return The value, or `error::payload_too_large` when the text is too long.
        [[nodiscard]] static constexpr std::expected<string_value, error> make(const std::string_view text) noexcept
        {
            if (text.size() > capacity)
                return std::unexpected(error::payload_too_large);

            string_value result {};
            for (std::size_t i {}; i < text.size(); ++i)
                result.data[i] = text[i];

            return result;
        }

        /// @brief Returns the characters up to the first zero.
        [[nodiscard]] constexpr std::string_view view() const noexcept
        {
            std::size_t size {};
            while ((size < capacity) && (data[size] != '\0'))
                ++size;

            return {data.data(), size};
        }

        /// @brief Compares two values character by character.
        [[nodiscard]] constexpr bool operator==(const string_value&) const noexcept = default;
    };
}
#endif // KMX_AIO_FEATURE_KNX
