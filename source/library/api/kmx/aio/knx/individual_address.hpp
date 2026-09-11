/// @file api/kmx/aio/knx/individual_address.hpp
/// @brief KNX individual (physical) device address value type.
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
        #include <kmx/aio/knx/group_address.hpp>

        #include <compare>
        #include <cstdint>
        #include <expected>
        #include <functional>
        #include <string>
        #include <string_view>
    #endif

namespace kmx::aio::knx
{
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
}
#endif // KMX_AIO_FEATURE_KNX
