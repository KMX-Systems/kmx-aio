/// @file api/kmx/aio/knx/address.hpp
/// @brief Decimal text helpers shared by the KNX individual and group address types.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// Both address kinds print and parse their components with the same routines, so they live here rather
/// than in either type's header. Every routine is `constexpr` and allocation-free.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/error.hpp>

        #include <cstdint>
        #include <expected>
        #include <string_view>
    #endif

namespace kmx::aio::knx
{
    namespace detail
    {
        /// @brief Largest decimal text length of any KNX address, including separators.
        /// @details `31/7/255` and `15.15.255` both fit in nine characters.
        inline constexpr std::size_t max_address_text_size = 9u;

        /// @brief Largest number of decimal digits a 16-bit address component can need.
        /// @details Five, for the free group address style, which prints the whole wire value as one number.
        ///          The level styles never exceed three, but this is the same routine.
        inline constexpr std::size_t max_decimal_digits = 5u;

        /// @brief Writes one unsigned decimal number into a character buffer.
        /// @param dest The destination buffer.
        /// @param offset The index to write the first digit at.
        /// @param value The value to write.
        /// @return The index one past the last written character.
        [[nodiscard]] constexpr std::size_t append_decimal(const span_char_t dest, const std::size_t offset,
                                                           const std::uint16_t value) noexcept
        {
            char digits[max_decimal_digits] {};
            std::size_t count {};
            std::uint16_t rest = value;
            do
            {
                digits[count] = static_cast<char>('0' + (rest % 10u));
                ++count;
                rest = static_cast<std::uint16_t>(rest / 10u);
            } while (rest != 0u);

            std::size_t next = offset;
            while (count != 0u)
            {
                --count;
                dest[next] = digits[count];
                ++next;
            }

            return next;
        }

        /// @brief Reads one unsigned decimal number from text.
        /// @param text The text to read from.
        /// @param offset The index to start at; advanced past the digits that were consumed.
        /// @param limit The largest accepted value.
        /// @return The parsed value, or `error::invalid_address` when no digit is present or the limit is exceeded.
        [[nodiscard]] constexpr std::expected<std::uint16_t, error> read_decimal(const std::string_view text, std::size_t& offset,
                                                                                 const std::uint16_t limit) noexcept
        {
            std::uint32_t value {};
            std::size_t digits {};
            while ((offset < text.size()) && (text[offset] >= '0') && (text[offset] <= '9'))
            {
                value = (value * 10u) + static_cast<std::uint32_t>(text[offset] - '0');
                if (value > limit)
                    return std::unexpected(error::invalid_address);

                ++offset;
                ++digits;
            }

            if (digits == 0u)
                return std::unexpected(error::invalid_address);

            return static_cast<std::uint16_t>(value);
        }

        /// @brief Consumes one expected separator character.
        /// @param text The text to read from.
        /// @param offset The index to read at; advanced by one on success.
        /// @param separator The character that must appear.
        /// @return Nothing, or `error::invalid_address` when the character does not appear.
        [[nodiscard]] constexpr std::expected<void, error> expect_separator(const std::string_view text, std::size_t& offset,
                                                                            const char separator) noexcept
        {
            if ((offset >= text.size()) || (text[offset] != separator))
                return std::unexpected(error::invalid_address);

            ++offset;
            return {};
        }
    }
}
#endif // KMX_AIO_FEATURE_KNX
