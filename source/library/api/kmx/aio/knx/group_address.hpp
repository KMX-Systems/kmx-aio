/// @file api/kmx/aio/knx/group_address.hpp
/// @brief KNX group address value type and the textual styles it is written in.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// Both KNX address kinds are 16-bit wire values that differ only in how the bits are grouped, and in
/// the address-type bit of the cEMI control field that says which grouping applies. Keeping them as
/// distinct types rather than raw integers makes that distinction a compile-time one: a group address
/// can never be passed where a device address is meant.
///
/// Every operation here is `constexpr` and allocation-free, so addresses can be built, formatted and
/// parsed inside constant expressions and checked with `static_assert`.
/// @reference KNX System Specifications, Volume 3/5/1 "Resources", address encoding.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/address.hpp>
        #include <kmx/aio/knx/error.hpp>
        #include <kmx/aio/knx/individual_address.hpp>

        #include <compare>
        #include <cstdint>
        #include <expected>
        #include <functional>
        #include <string>
        #include <string_view>
    #endif

namespace kmx::aio::knx
{
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
