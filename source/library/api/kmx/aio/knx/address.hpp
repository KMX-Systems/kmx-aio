/// @file aio/knx/address.hpp
/// @brief KNX individual and group address value types.
/// @details
/// Both KNX address kinds are 16-bit wire values that differ only in how the bits are grouped, and in
/// the address-type bit of the cEMI control field that says which grouping applies. Keeping them as
/// distinct types rather than raw integers makes that distinction a compile-time one: a group address
/// can never be passed where a device address is meant.
///
/// Every operation here is `constexpr` and allocation-free, so addresses can be built, formatted and
/// parsed inside constant expressions and checked with `static_assert`.
/// @reference KNX System Specifications, Volume 3/5/1 "Resources", address encoding.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <compare>
        #include <cstdint>
        #include <expected>
        #include <functional>
        #include <string>
        #include <string_view>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/error.hpp>

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

    /// @brief A KNX individual (physical) device address, written `area.line.device`.
    /// @details Bits 15..12 carry the area, bits 11..8 the line and bits 7..0 the device. The value
    ///          `0.0.0` is the unset address a tunnelling client sends before the server assigns one.
    class individual_address
    {
    public:
        /// @brief Highest accepted area number.
        static constexpr std::uint8_t max_area = 0x0Fu;
        /// @brief Highest accepted line number.
        static constexpr std::uint8_t max_line = 0x0Fu;
        /// @brief Highest accepted device number.
        static constexpr std::uint8_t max_device = 0xFFu;

        /// @brief Creates the unset address `0.0.0`.
        constexpr individual_address() noexcept = default;

        /// @brief Creates an address from its wire value.
        /// @param raw The 16-bit big-endian wire value in host order.
        constexpr explicit individual_address(const std::uint16_t raw) noexcept: value_(raw) {}

        /// @brief Creates an address from its three components.
        /// @param area The area, 0..15; higher bits are discarded.
        /// @param line The line, 0..15; higher bits are discarded.
        /// @param device The device, 0..255.
        /// @note Prefer @ref make when the components come from untrusted input.
        constexpr individual_address(const std::uint8_t area, const std::uint8_t line, const std::uint8_t device) noexcept:
            value_(static_cast<std::uint16_t>((static_cast<std::uint16_t>(area & max_area) << 12u) |
                                              (static_cast<std::uint16_t>(line & max_line) << 8u) | device))
        {
        }

        /// @brief Creates an address from three components, rejecting out-of-range ones.
        /// @param area The area, 0..15.
        /// @param line The line, 0..15.
        /// @param device The device, 0..255.
        /// @return The address, or `error::invalid_address` when a component does not fit its field.
        [[nodiscard]] static constexpr std::expected<individual_address, error> make(const std::uint16_t area, const std::uint16_t line,
                                                                                     const std::uint16_t device) noexcept
        {
            if ((area > max_area) || (line > max_line) || (device > max_device))
                return std::unexpected(error::invalid_address);

            return individual_address {static_cast<std::uint8_t>(area), static_cast<std::uint8_t>(line), static_cast<std::uint8_t>(device)};
        }

        /// @brief Parses the textual form `area.line.device`.
        /// @param text The text to parse; no surrounding whitespace is accepted.
        /// @return The address, or `error::invalid_address` when the text is not a complete address.
        [[nodiscard]] static constexpr std::expected<individual_address, error> parse(const std::string_view text) noexcept
        {
            std::size_t offset {};
            const auto area = detail::read_decimal(text, offset, max_area);
            if (!area.has_value())
                return std::unexpected(area.error());
            if (const auto dot = detail::expect_separator(text, offset, '.'); !dot.has_value())
                return std::unexpected(dot.error());

            const auto line = detail::read_decimal(text, offset, max_line);
            if (!line.has_value())
                return std::unexpected(line.error());
            if (const auto dot = detail::expect_separator(text, offset, '.'); !dot.has_value())
                return std::unexpected(dot.error());

            const auto device = detail::read_decimal(text, offset, max_device);
            if (!device.has_value())
                return std::unexpected(device.error());
            if (offset != text.size())
                return std::unexpected(error::invalid_address);

            return individual_address {static_cast<std::uint8_t>(*area), static_cast<std::uint8_t>(*line),
                                       static_cast<std::uint8_t>(*device)};
        }

        /// @brief Returns the 16-bit wire value in host order.
        [[nodiscard]] constexpr std::uint16_t value() const noexcept { return value_; }
        /// @brief Returns the area, 0..15.
        [[nodiscard]] constexpr std::uint8_t area() const noexcept { return static_cast<std::uint8_t>(value_ >> 12u); }
        /// @brief Returns the line, 0..15.
        [[nodiscard]] constexpr std::uint8_t line() const noexcept { return static_cast<std::uint8_t>((value_ >> 8u) & max_line); }
        /// @brief Returns the device, 0..255.
        [[nodiscard]] constexpr std::uint8_t device() const noexcept { return static_cast<std::uint8_t>(value_ & max_device); }
        /// @brief Indicates whether this is the unset address `0.0.0`.
        [[nodiscard]] constexpr bool unset() const noexcept { return value_ == 0u; }

        /// @brief Writes the textual form `area.line.device` into a caller-supplied buffer.
        /// @param dest The buffer to write into; at least @ref detail::max_address_text_size characters.
        /// @return The number of characters written, or `error::invalid_length` when the buffer is too small.
        [[nodiscard]] constexpr std::expected<std::size_t, error> format(const span_char_t dest) const noexcept
        {
            if (dest.size() < detail::max_address_text_size)
                return std::unexpected(error::invalid_length);

            std::size_t offset = detail::append_decimal(dest, 0u, area());
            dest[offset] = '.';
            offset = detail::append_decimal(dest, offset + 1u, line());
            dest[offset] = '.';
            return detail::append_decimal(dest, offset + 1u, device());
        }

        /// @brief Returns the textual form `area.line.device`.
        [[nodiscard]] std::string to_string() const noexcept(false);

        /// @brief Compares two addresses by wire value.
        [[nodiscard]] constexpr auto operator<=>(const individual_address&) const noexcept = default;

    private:
        /// @brief The 16-bit wire value in host order.
        std::uint16_t value_ {};
    };

    /// @brief The textual style a group address is written in.
    enum class group_address_style : std::uint8_t
    {
        /// @brief `main/sub`, with an 11-bit sub-group.
        two_level,
        /// @brief `main/middle/sub`, the ETS default.
        three_level,
        /// @brief A single number, `0`..`65535`.
        free,
    };

    /// @brief A KNX group address, the destination of group communication.
    /// @details The same 16 bits are read as `main/middle/sub`, `main/sub` or one free number; the style
    ///          is a presentation choice made by the installation, never a property of the wire value.
    ///          Address `0` is the broadcast address and is not a usable group.
    class group_address
    {
    public:
        /// @brief Highest accepted main group in both level styles.
        static constexpr std::uint8_t max_main = 0x1Fu;
        /// @brief Highest accepted middle group in the three-level style.
        static constexpr std::uint8_t max_middle = 0x07u;
        /// @brief Highest accepted sub group in the three-level style.
        static constexpr std::uint8_t max_sub_three_level = 0xFFu;
        /// @brief Highest accepted sub group in the two-level style.
        static constexpr std::uint16_t max_sub_two_level = 0x07FFu;

        /// @brief Creates the broadcast address `0`.
        constexpr group_address() noexcept = default;

        /// @brief Creates a group address from its wire value.
        /// @param raw The 16-bit big-endian wire value in host order.
        constexpr explicit group_address(const std::uint16_t raw) noexcept: value_(raw) {}

        /// @brief Creates a group address from three-level components.
        /// @param main The main group, 0..31.
        /// @param middle The middle group, 0..7.
        /// @param sub The sub group, 0..255.
        /// @return The address, or `error::invalid_address` when a component does not fit its field.
        [[nodiscard]] static constexpr std::expected<group_address, error> make(const std::uint16_t main, const std::uint16_t middle,
                                                                                const std::uint16_t sub) noexcept
        {
            if ((main > max_main) || (middle > max_middle) || (sub > max_sub_three_level))
                return std::unexpected(error::invalid_address);

            return group_address {static_cast<std::uint16_t>((main << 11u) | (middle << 8u) | sub)};
        }

        /// @brief Creates a group address from two-level components.
        /// @param main The main group, 0..31.
        /// @param sub The sub group, 0..2047.
        /// @return The address, or `error::invalid_address` when a component does not fit its field.
        [[nodiscard]] static constexpr std::expected<group_address, error> make(const std::uint16_t main, const std::uint16_t sub) noexcept
        {
            if ((main > max_main) || (sub > max_sub_two_level))
                return std::unexpected(error::invalid_address);

            return group_address {static_cast<std::uint16_t>((main << 11u) | sub)};
        }

        /// @brief Parses `main/middle/sub`, `main/sub` or a single free number.
        /// @param text The text to parse; no surrounding whitespace is accepted.
        /// @return The address, or `error::invalid_address` when the text is not a complete address.
        /// @note The style follows the text, so a round trip through @ref format needs the same style back.
        [[nodiscard]] static constexpr std::expected<group_address, error> parse(const std::string_view text) noexcept
        {
            std::size_t offset {};
            const auto first = detail::read_decimal(text, offset, 0xFFFFu);
            if (!first.has_value())
                return std::unexpected(first.error());
            if (offset == text.size())
                return group_address {*first};
            if (const auto slash = detail::expect_separator(text, offset, '/'); !slash.has_value())
                return std::unexpected(slash.error());

            const auto second = detail::read_decimal(text, offset, max_sub_two_level);
            if (!second.has_value())
                return std::unexpected(second.error());
            if (offset == text.size())
                return make(*first, *second);
            if (const auto slash = detail::expect_separator(text, offset, '/'); !slash.has_value())
                return std::unexpected(slash.error());

            const auto third = detail::read_decimal(text, offset, max_sub_three_level);
            if (!third.has_value())
                return std::unexpected(third.error());
            if (offset != text.size())
                return std::unexpected(error::invalid_address);

            return make(*first, *second, *third);
        }

        /// @brief Returns the 16-bit wire value in host order.
        [[nodiscard]] constexpr std::uint16_t value() const noexcept { return value_; }
        /// @brief Returns the main group, 0..31.
        [[nodiscard]] constexpr std::uint8_t main_group() const noexcept { return static_cast<std::uint8_t>(value_ >> 11u); }
        /// @brief Returns the middle group of the three-level style, 0..7.
        [[nodiscard]] constexpr std::uint8_t middle_group() const noexcept
        {
            return static_cast<std::uint8_t>((value_ >> 8u) & max_middle);
        }
        /// @brief Returns the sub group of the three-level style, 0..255.
        [[nodiscard]] constexpr std::uint8_t sub_group() const noexcept { return static_cast<std::uint8_t>(value_ & 0xFFu); }
        /// @brief Returns the sub group of the two-level style, 0..2047.
        [[nodiscard]] constexpr std::uint16_t sub_group_two_level() const noexcept
        {
            return static_cast<std::uint16_t>(value_ & max_sub_two_level);
        }
        /// @brief Indicates whether this is the broadcast address `0`.
        [[nodiscard]] constexpr bool broadcast() const noexcept { return value_ == 0u; }

        /// @brief Writes the textual form in the requested style into a caller-supplied buffer.
        /// @param dest The buffer to write into; at least @ref detail::max_address_text_size characters.
        /// @param style The style to write in.
        /// @return The number of characters written, or `error::invalid_length` when the buffer is too small.
        [[nodiscard]] constexpr std::expected<std::size_t, error> format(
            const span_char_t dest, const group_address_style style = group_address_style::three_level) const noexcept
        {
            if (dest.size() < detail::max_address_text_size)
                return std::unexpected(error::invalid_length);

            if (style == group_address_style::free)
                return detail::append_decimal(dest, 0u, value_);

            std::size_t offset = detail::append_decimal(dest, 0u, main_group());
            dest[offset] = '/';
            if (style == group_address_style::two_level)
                return detail::append_decimal(dest, offset + 1u, sub_group_two_level());

            offset = detail::append_decimal(dest, offset + 1u, middle_group());
            dest[offset] = '/';
            return detail::append_decimal(dest, offset + 1u, sub_group());
        }

        /// @brief Returns the textual form in the requested style.
        /// @param style The style to write in.
        [[nodiscard]] std::string to_string(const group_address_style style = group_address_style::three_level) const noexcept(false);

        /// @brief Compares two addresses by wire value.
        [[nodiscard]] constexpr auto operator<=>(const group_address&) const noexcept = default;

    private:
        /// @brief The 16-bit wire value in host order.
        std::uint16_t value_ {};
    };
}

namespace std
{
    /// @brief Hash support so an individual address can key an unordered container.
    template <>
    struct hash<kmx::aio::knx::individual_address>
    {
        /// @brief Hashes the address by its wire value.
        /// @param value The address to hash.
        /// @return The hash of the wire value.
        [[nodiscard]] std::size_t operator()(const kmx::aio::knx::individual_address value) const noexcept
        {
            return hash<std::uint16_t> {}(value.value());
        }
    };

    /// @brief Hash support so a group address can key an unordered container.
    template <>
    struct hash<kmx::aio::knx::group_address>
    {
        /// @brief Hashes the address by its wire value.
        /// @param value The address to hash.
        /// @return The hash of the wire value.
        [[nodiscard]] std::size_t operator()(const kmx::aio::knx::group_address value) const noexcept
        {
            return hash<std::uint16_t> {}(value.value());
        }
    };
}
#endif // KMX_AIO_FEATURE_KNX
