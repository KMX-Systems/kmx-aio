/// @file api/kmx/aio/knx/dpt/traits.hpp
/// @brief The wire format of every KNX datapoint main type this build implements.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// A datapoint type is written `main.sub`. The main type fixes the wire format — how many bits, how they
/// are laid out, how a number is scaled — and the sub type fixes only the unit and the range, so the
/// traits below are keyed on the main type and the sub types that need a different range appear as named
/// helpers in `kmx/aio/knx/dpt.hpp`.
///
/// Everything here is `constexpr` and allocation-free.
/// @reference KNX System Specifications, Volume 3/7/2 "Datapoint Types".
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/knx/dpt/descriptor.hpp>
        #include <kmx/aio/knx/dpt/payload.hpp>
        #include <kmx/aio/knx/dpt/string_value.hpp>
        #include <kmx/aio/knx/dpt/value_view.hpp>
        #include <kmx/aio/knx/error.hpp>

        #include <array>
        #include <bit>
        #include <cstdint>
        #include <expected>
    #endif

namespace kmx::aio::knx::dpt
{
    namespace detail
    {
        /// @brief Reads a compact value from either wire form.
        /// @param value The received value.
        /// @param mask The mask of the bits the datapoint occupies.
        /// @return The masked value, or `error::unsupported_datapoint` when the value is wider than one octet.
        /// @note A value narrower than seven bits belongs in the APCI octet, but devices exist that send it
        ///       as a whole octet instead. Both are accepted on receive; only the compact form is sent.
        [[nodiscard]] constexpr std::expected<std::uint8_t, error> read_compact(const value_view& value, const std::uint8_t mask) noexcept
        {
            if (value.compacted)
                return static_cast<std::uint8_t>(value.compact_value & mask);
            if (value.octets.size() != 1u)
                return std::unexpected(error::unsupported_datapoint);

            return static_cast<std::uint8_t>(value.octets[0u] & mask);
        }

        /// @brief Reads a fixed-width unsigned value from the payload octets.
        /// @tparam Size The expected number of octets.
        /// @param value The received value.
        /// @return The value in host order, or `error::unsupported_datapoint` on a width mismatch.
        template <std::size_t Size>
        [[nodiscard]] constexpr std::expected<std::uint32_t, error> read_unsigned(const value_view& value) noexcept
        {
            static_assert(Size <= 4u, "read_unsigned handles at most four octets");
            if (value.compacted || (value.octets.size() != Size))
                return std::unexpected(error::unsupported_datapoint);

            std::uint32_t result {};
            for (std::size_t i {}; i < Size; ++i)
                result = (result << 8u) | value.octets[i];

            return result;
        }

        /// @brief Writes a fixed-width unsigned value into a payload.
        /// @tparam Size The number of octets to write.
        /// @param raw The value in host order.
        /// @return The encoded payload.
        template <std::size_t Size>
        [[nodiscard]] constexpr payload write_unsigned(const std::uint32_t raw) noexcept
        {
            static_assert(Size <= 4u, "write_unsigned handles at most four octets");
            std::array<std::uint8_t, Size> octets {};
            for (std::size_t i {}; i < Size; ++i)
                octets[i] = static_cast<std::uint8_t>(raw >> (8u * (Size - 1u - i)));

            return payload::octets(octets).value();
        }

        /// @brief Reinterprets a 32-bit pattern as an IEEE 754 single-precision number.
        /// @param raw The bit pattern.
        /// @return The number the pattern encodes.
        [[nodiscard]] constexpr float bits_to_float(const std::uint32_t raw) noexcept
        {
            return std::bit_cast<float>(raw);
        }

        /// @brief Reinterprets an IEEE 754 single-precision number as a 32-bit pattern.
        /// @param value The number.
        /// @return The bit pattern that encodes it.
        [[nodiscard]] constexpr std::uint32_t float_to_bits(const float value) noexcept
        {
            return std::bit_cast<std::uint32_t>(value);
        }

        /// @brief Rounds a number to the nearest integer, halves away from zero.
        /// @param value The number to round.
        /// @return The rounded value.
        [[nodiscard]] constexpr std::int32_t round_to_int(const double value) noexcept
        {
            return static_cast<std::int32_t>((value >= 0.0) ? (value + 0.5) : (value - 0.5));
        }
    }

    /// @brief A one-bit value together with the control bit that qualifies it, for main type 2.
    struct controlled_bool
    {
        /// @brief Whether the control bit is set.
        bool control {};
        /// @brief The value bit.
        bool value {};

        /// @brief Compares two values field by field.
        [[nodiscard]] constexpr bool operator==(const controlled_bool&) const noexcept = default;
    };

    /// @brief A stepwise dimming or blind command, for main type 3.
    struct control_step
    {
        /// @brief Whether the command increases the value, or moves the blind down.
        bool increase {};
        /// @brief The step code, 1..7 as an interval of `2^(code-1)`; zero is the break command.
        std::uint8_t step_code {};

        /// @brief Compares two commands field by field.
        [[nodiscard]] constexpr bool operator==(const control_step&) const noexcept = default;
    };

    /// @brief A time of day with an optional weekday, for main type 10.
    struct time_of_day
    {
        /// @brief The weekday, 0 for none and 1..7 for Monday to Sunday.
        std::uint8_t weekday {};
        /// @brief The hour, 0..23.
        std::uint8_t hour {};
        /// @brief The minute, 0..59.
        std::uint8_t minute {};
        /// @brief The second, 0..59.
        std::uint8_t second {};

        /// @brief Compares two times field by field.
        [[nodiscard]] constexpr bool operator==(const time_of_day&) const noexcept = default;
    };

    /// @brief A calendar date, for main type 11.
    struct date
    {
        /// @brief The day of the month, 1..31.
        std::uint8_t day {};
        /// @brief The month, 1..12.
        std::uint8_t month {};
        /// @brief The year; only 1990..2089 can be represented.
        std::uint16_t year {};

        /// @brief Compares two dates field by field.
        [[nodiscard]] constexpr bool operator==(const date&) const noexcept = default;
    };

    /// @brief A scene recall or store command, for main type 18.
    struct scene_control
    {
        /// @brief Whether the scene is to be stored rather than recalled.
        bool learn {};
        /// @brief The scene number, 0..63.
        std::uint8_t scene {};

        /// @brief Compares two commands field by field.
        [[nodiscard]] constexpr bool operator==(const scene_control&) const noexcept = default;
    };

    /// @brief An RGB colour, for main type 232.
    struct rgb
    {
        /// @brief The red component.
        std::uint8_t red {};
        /// @brief The green component.
        std::uint8_t green {};
        /// @brief The blue component.
        std::uint8_t blue {};

        /// @brief Compares two colours component by component.
        [[nodiscard]] constexpr bool operator==(const rgb&) const noexcept = default;
    };

    /// @brief The wire format of one datapoint main type.
    /// @tparam Main The datapoint main type.
    /// @note Only the specialisations below exist. A user may add another for a main type this build does
    ///       not carry without changing the library, as long as it provides the same three members.
    template <std::uint16_t Main>
    struct traits;

    /// @brief The C++ value type of one datapoint main type.
    /// @tparam Main The datapoint main type.
    template <std::uint16_t Main>
    using value_t = typename traits<Main>::value_t;

    /// @brief A decoded datapoint value, or the reason it could not be decoded.
    /// @tparam Main The datapoint main type.
    template <std::uint16_t Main>
    using decode_result_t = std::expected<value_t<Main>, error>;

    /// @brief An encoded datapoint payload, or the reason it could not be encoded.
    using encode_result_t = std::expected<payload, error>;

    /// @brief Main type 1 — a single bit, such as switch, bool, alarm or step.
    template <>
    struct traits<1u>
    {
        /// @brief The C++ type of the value.
        using value_t = bool;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(1u).value();

        /// @brief Encodes a bit.
        /// @param value The value to encode.
        /// @return The encoded payload.
        [[nodiscard]] static constexpr encode_result_t encode(const bool value) noexcept { return payload::compact(value ? 1u : 0u); }

        /// @brief Decodes a bit.
        /// @param value The received value.
        /// @return The decoded value, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<bool, error> decode(const value_view& value) noexcept
        {
            const auto raw = detail::read_compact(value, 0x01u);
            if (!raw.has_value())
                return std::unexpected(raw.error());

            return *raw != 0u;
        }
    };

    /// @brief Main type 2 — a bit with a control bit, such as switch control.
    template <>
    struct traits<2u>
    {
        /// @brief The C++ type of the value.
        using value_t = controlled_bool;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(2u).value();

        /// @brief Encodes a controlled bit.
        /// @param value The value to encode.
        /// @return The encoded payload.
        [[nodiscard]] static constexpr encode_result_t encode(const controlled_bool value) noexcept
        {
            return payload::compact(static_cast<std::uint8_t>((value.control ? 0x02u : 0x00u) | (value.value ? 0x01u : 0x00u)));
        }

        /// @brief Decodes a controlled bit.
        /// @param value The received value.
        /// @return The decoded value, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<controlled_bool, error> decode(const value_view& value) noexcept
        {
            const auto raw = detail::read_compact(value, 0x03u);
            if (!raw.has_value())
                return std::unexpected(raw.error());

            return controlled_bool {.control = (*raw & 0x02u) != 0u, .value = (*raw & 0x01u) != 0u};
        }
    };

    /// @brief Main type 3 — a stepwise dimming or blind command.
    template <>
    struct traits<3u>
    {
        /// @brief The C++ type of the value.
        using value_t = control_step;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(3u).value();

        /// @brief Encodes a step command.
        /// @param value The command to encode.
        /// @return The encoded payload, or `error::value_out_of_range` when the step code exceeds seven.
        [[nodiscard]] static constexpr encode_result_t encode(const control_step value) noexcept
        {
            if (value.step_code > 0x07u)
                return std::unexpected(error::value_out_of_range);

            return payload::compact(static_cast<std::uint8_t>((value.increase ? 0x08u : 0x00u) | value.step_code));
        }

        /// @brief Decodes a step command.
        /// @param value The received value.
        /// @return The decoded command, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<control_step, error> decode(const value_view& value) noexcept
        {
            const auto raw = detail::read_compact(value, 0x0Fu);
            if (!raw.has_value())
                return std::unexpected(raw.error());

            return control_step {.increase = (*raw & 0x08u) != 0u, .step_code = static_cast<std::uint8_t>(*raw & 0x07u)};
        }
    };

    /// @brief Main type 4 — one character.
    template <>
    struct traits<4u>
    {
        /// @brief The C++ type of the value.
        using value_t = char;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(4u).value();

        /// @brief Encodes a character.
        /// @param value The character to encode.
        /// @return The encoded payload.
        [[nodiscard]] static constexpr encode_result_t encode(const char value) noexcept
        {
            return detail::write_unsigned<1u>(static_cast<std::uint8_t>(value));
        }

        /// @brief Decodes a character.
        /// @param value The received value.
        /// @return The decoded character, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<char, error> decode(const value_view& value) noexcept
        {
            const auto raw = detail::read_unsigned<1u>(value);
            if (!raw.has_value())
                return std::unexpected(raw.error());

            return static_cast<char>(*raw);
        }
    };

    /// @brief Main type 5 — an 8-bit unsigned number, the raw form behind percent and angle.
    template <>
    struct traits<5u>
    {
        /// @brief The C++ type of the value.
        using value_t = std::uint8_t;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(5u).value();

        /// @brief Encodes an 8-bit unsigned number.
        /// @param value The value to encode.
        /// @return The encoded payload.
        [[nodiscard]] static constexpr encode_result_t encode(const std::uint8_t value) noexcept
        {
            return detail::write_unsigned<1u>(value);
        }

        /// @brief Decodes an 8-bit unsigned number.
        /// @param value The received value.
        /// @return The decoded value, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<std::uint8_t, error> decode(const value_view& value) noexcept
        {
            const auto raw = detail::read_unsigned<1u>(value);
            if (!raw.has_value())
                return std::unexpected(raw.error());

            return static_cast<std::uint8_t>(*raw);
        }
    };

    /// @brief Main type 6 — an 8-bit signed number.
    template <>
    struct traits<6u>
    {
        /// @brief The C++ type of the value.
        using value_t = std::int8_t;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(6u).value();

        /// @brief Encodes an 8-bit signed number.
        /// @param value The value to encode.
        /// @return The encoded payload.
        [[nodiscard]] static constexpr encode_result_t encode(const std::int8_t value) noexcept
        {
            return detail::write_unsigned<1u>(static_cast<std::uint8_t>(value));
        }

        /// @brief Decodes an 8-bit signed number.
        /// @param value The received value.
        /// @return The decoded value, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<std::int8_t, error> decode(const value_view& value) noexcept
        {
            const auto raw = detail::read_unsigned<1u>(value);
            if (!raw.has_value())
                return std::unexpected(raw.error());

            return static_cast<std::int8_t>(static_cast<std::uint8_t>(*raw));
        }
    };

    /// @brief Main type 7 — a 2-octet unsigned number.
    template <>
    struct traits<7u>
    {
        /// @brief The C++ type of the value.
        using value_t = std::uint16_t;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(7u).value();

        /// @brief Encodes a 2-octet unsigned number.
        /// @param value The value to encode.
        /// @return The encoded payload.
        [[nodiscard]] static constexpr encode_result_t encode(const std::uint16_t value) noexcept
        {
            return detail::write_unsigned<2u>(value);
        }

        /// @brief Decodes a 2-octet unsigned number.
        /// @param value The received value.
        /// @return The decoded value, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<std::uint16_t, error> decode(const value_view& value) noexcept
        {
            const auto raw = detail::read_unsigned<2u>(value);
            if (!raw.has_value())
                return std::unexpected(raw.error());

            return static_cast<std::uint16_t>(*raw);
        }
    };

    /// @brief Main type 8 — a 2-octet signed number.
    template <>
    struct traits<8u>
    {
        /// @brief The C++ type of the value.
        using value_t = std::int16_t;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(8u).value();

        /// @brief Encodes a 2-octet signed number.
        /// @param value The value to encode.
        /// @return The encoded payload.
        [[nodiscard]] static constexpr encode_result_t encode(const std::int16_t value) noexcept
        {
            return detail::write_unsigned<2u>(static_cast<std::uint16_t>(value));
        }

        /// @brief Decodes a 2-octet signed number.
        /// @param value The received value.
        /// @return The decoded value, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<std::int16_t, error> decode(const value_view& value) noexcept
        {
            const auto raw = detail::read_unsigned<2u>(value);
            if (!raw.has_value())
                return std::unexpected(raw.error());

            return static_cast<std::int16_t>(static_cast<std::uint16_t>(*raw));
        }
    };

    /// @brief Main type 9 — the 2-octet KNX float used by every temperature and humidity sensor.
    /// @details The value is `0.01 * mantissa * 2^exponent`, with an 11-bit two's complement mantissa and a
    ///          4-bit exponent, which gives a range of -671088.64 to 670760.96 at a resolution that halves
    ///          with every doubling of the magnitude.
    template <>
    struct traits<9u>
    {
        /// @brief The C++ type of the value.
        using value_t = float;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(9u).value();
        /// @brief The smallest representable value.
        static constexpr float min_value = -671088.64f;
        /// @brief The largest representable value.
        static constexpr float max_value = 670760.96f;

        /// @brief Encodes a number in the 2-octet KNX float format.
        /// @param value The value to encode.
        /// @return The encoded payload, or `error::value_out_of_range` when the value cannot be represented.
        [[nodiscard]] static constexpr encode_result_t encode(const float value) noexcept
        {
            if (!(value >= min_value) || !(value <= max_value))
                return std::unexpected(error::value_out_of_range);

            double scaled = static_cast<double>(value) * 100.0;
            std::uint16_t exponent {};
            std::int32_t mantissa = detail::round_to_int(scaled);
            while ((mantissa < -2048) || (mantissa > 2047))
            {
                if (exponent == 15u)
                    return std::unexpected(error::value_out_of_range);

                scaled /= 2.0;
                ++exponent;
                mantissa = detail::round_to_int(scaled);
            }

            const auto raw = static_cast<std::uint16_t>(((mantissa < 0) ? 0x8000u : 0x0000u) | static_cast<std::uint16_t>(exponent << 11u) |
                                                        (static_cast<std::uint32_t>(mantissa) & 0x07FFu));
            return detail::write_unsigned<2u>(raw);
        }

        /// @brief Decodes a number in the 2-octet KNX float format.
        /// @param value The received value.
        /// @return The decoded value, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<float, error> decode(const value_view& value) noexcept
        {
            const auto raw = detail::read_unsigned<2u>(value);
            if (!raw.has_value())
                return std::unexpected(raw.error());

            const auto word = static_cast<std::uint16_t>(*raw);
            const auto exponent = static_cast<std::int32_t>((word >> 11u) & 0x0Fu);
            auto mantissa = static_cast<std::int32_t>(word & 0x07FFu);
            if ((word & 0x8000u) != 0u)
                mantissa -= 2048;

            return static_cast<float>(mantissa * (std::int32_t {1} << exponent)) / 100.0f;
        }
    };

    /// @brief Main type 10 — a time of day with an optional weekday.
    template <>
    struct traits<10u>
    {
        /// @brief The C++ type of the value.
        using value_t = time_of_day;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(10u).value();

        /// @brief Encodes a time of day.
        /// @param value The time to encode.
        /// @return The encoded payload, or `error::value_out_of_range` when a field is out of range.
        [[nodiscard]] static constexpr encode_result_t encode(const time_of_day value) noexcept
        {
            if ((value.weekday > 7u) || (value.hour > 23u) || (value.minute > 59u) || (value.second > 59u))
                return std::unexpected(error::value_out_of_range);

            const std::array<std::uint8_t, 3u> octets {static_cast<std::uint8_t>((value.weekday << 5u) | value.hour), value.minute,
                                                       value.second};
            return payload::octets(octets);
        }

        /// @brief Decodes a time of day.
        /// @param value The received value.
        /// @return The decoded time, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<time_of_day, error> decode(const value_view& value) noexcept
        {
            if (value.compacted || (value.octets.size() != 3u))
                return std::unexpected(error::unsupported_datapoint);

            const time_of_day decoded {.weekday = static_cast<std::uint8_t>(value.octets[0u] >> 5u),
                                       .hour = static_cast<std::uint8_t>(value.octets[0u] & 0x1Fu),
                                       .minute = static_cast<std::uint8_t>(value.octets[1u] & 0x3Fu),
                                       .second = static_cast<std::uint8_t>(value.octets[2u] & 0x3Fu)};
            if ((decoded.hour > 23u) || (decoded.minute > 59u) || (decoded.second > 59u))
                return std::unexpected(error::value_out_of_range);

            return decoded;
        }
    };

    /// @brief Main type 11 — a calendar date.
    /// @note The wire format carries two year digits. Values 0 to 89 mean 2000 to 2089, and 90 to 99 mean
    ///       1990 to 1999, which is the whole range the format can express.
    template <>
    struct traits<11u>
    {
        /// @brief The C++ type of the value.
        using value_t = date;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(11u).value();

        /// @brief Encodes a date.
        /// @param value The date to encode.
        /// @return The encoded payload, or `error::value_out_of_range` when a field is out of range.
        [[nodiscard]] static constexpr encode_result_t encode(const date value) noexcept
        {
            if ((value.day < 1u) || (value.day > 31u) || (value.month < 1u) || (value.month > 12u))
                return std::unexpected(error::value_out_of_range);
            if (((value.year < 1990u) || (value.year > 2089u)))
                return std::unexpected(error::value_out_of_range);

            const auto year = static_cast<std::uint8_t>((value.year >= 2000u) ? (value.year - 2000u) : (value.year - 1900u));
            const std::array<std::uint8_t, 3u> octets {value.day, value.month, year};
            return payload::octets(octets);
        }

        /// @brief Decodes a date.
        /// @param value The received value.
        /// @return The decoded date, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<date, error> decode(const value_view& value) noexcept
        {
            if (value.compacted || (value.octets.size() != 3u))
                return std::unexpected(error::unsupported_datapoint);

            const auto day = static_cast<std::uint8_t>(value.octets[0u] & 0x1Fu);
            const auto month = static_cast<std::uint8_t>(value.octets[1u] & 0x0Fu);
            const auto year = static_cast<std::uint8_t>(value.octets[2u] & 0x7Fu);
            if ((day < 1u) || (day > 31u) || (month < 1u) || (month > 12u) || (year > 99u))
                return std::unexpected(error::value_out_of_range);

            return date {.day = day, .month = month, .year = static_cast<std::uint16_t>((year < 90u) ? (2000u + year) : (1900u + year))};
        }
    };

    /// @brief Main type 12 — a 4-octet unsigned number.
    template <>
    struct traits<12u>
    {
        /// @brief The C++ type of the value.
        using value_t = std::uint32_t;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(12u).value();

        /// @brief Encodes a 4-octet unsigned number.
        /// @param value The value to encode.
        /// @return The encoded payload.
        [[nodiscard]] static constexpr encode_result_t encode(const std::uint32_t value) noexcept
        {
            return detail::write_unsigned<4u>(value);
        }

        /// @brief Decodes a 4-octet unsigned number.
        /// @param value The received value.
        /// @return The decoded value, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<std::uint32_t, error> decode(const value_view& value) noexcept
        {
            return detail::read_unsigned<4u>(value);
        }
    };

    /// @brief Main type 13 — a 4-octet signed number.
    template <>
    struct traits<13u>
    {
        /// @brief The C++ type of the value.
        using value_t = std::int32_t;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(13u).value();

        /// @brief Encodes a 4-octet signed number.
        /// @param value The value to encode.
        /// @return The encoded payload.
        [[nodiscard]] static constexpr encode_result_t encode(const std::int32_t value) noexcept
        {
            return detail::write_unsigned<4u>(static_cast<std::uint32_t>(value));
        }

        /// @brief Decodes a 4-octet signed number.
        /// @param value The received value.
        /// @return The decoded value, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<std::int32_t, error> decode(const value_view& value) noexcept
        {
            const auto raw = detail::read_unsigned<4u>(value);
            if (!raw.has_value())
                return std::unexpected(raw.error());

            return static_cast<std::int32_t>(*raw);
        }
    };

    /// @brief Main type 14 — a 4-octet IEEE 754 single-precision number.
    template <>
    struct traits<14u>
    {
        /// @brief The C++ type of the value.
        using value_t = float;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(14u).value();

        /// @brief Encodes a single-precision number.
        /// @param value The value to encode.
        /// @return The encoded payload.
        [[nodiscard]] static constexpr encode_result_t encode(const float value) noexcept
        {
            return detail::write_unsigned<4u>(detail::float_to_bits(value));
        }

        /// @brief Decodes a single-precision number.
        /// @param value The received value.
        /// @return The decoded value, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<float, error> decode(const value_view& value) noexcept
        {
            const auto raw = detail::read_unsigned<4u>(value);
            if (!raw.has_value())
                return std::unexpected(raw.error());

            return detail::bits_to_float(*raw);
        }
    };

    /// @brief Main type 16 — a fourteen character string.
    template <>
    struct traits<16u>
    {
        /// @brief The C++ type of the value.
        using value_t = string_value;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(16u).value();

        /// @brief Encodes a string.
        /// @param value The string to encode.
        /// @return The encoded payload.
        [[nodiscard]] static constexpr encode_result_t encode(const string_value& value) noexcept
        {
            std::array<std::uint8_t, string_value::capacity> octets {};
            for (std::size_t i {}; i < string_value::capacity; ++i)
                octets[i] = static_cast<std::uint8_t>(value.data[i]);

            return payload::octets(octets);
        }

        /// @brief Decodes a string.
        /// @param value The received value.
        /// @return The decoded string, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<string_value, error> decode(const value_view& value) noexcept
        {
            if (value.compacted || (value.octets.size() != string_value::capacity))
                return std::unexpected(error::unsupported_datapoint);

            string_value decoded {};
            for (std::size_t i {}; i < string_value::capacity; ++i)
                decoded.data[i] = static_cast<char>(value.octets[i]);

            return decoded;
        }
    };

    /// @brief Main type 17 — a scene number.
    template <>
    struct traits<17u>
    {
        /// @brief The C++ type of the value.
        using value_t = std::uint8_t;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(17u).value();

        /// @brief Encodes a scene number.
        /// @param value The scene number, 0..63.
        /// @return The encoded payload, or `error::value_out_of_range` when the number exceeds 63.
        [[nodiscard]] static constexpr encode_result_t encode(const std::uint8_t value) noexcept
        {
            if (value > 0x3Fu)
                return std::unexpected(error::value_out_of_range);

            return detail::write_unsigned<1u>(value);
        }

        /// @brief Decodes a scene number.
        /// @param value The received value.
        /// @return The decoded scene number, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<std::uint8_t, error> decode(const value_view& value) noexcept
        {
            const auto raw = detail::read_unsigned<1u>(value);
            if (!raw.has_value())
                return std::unexpected(raw.error());

            return static_cast<std::uint8_t>(*raw & 0x3Fu);
        }
    };

    /// @brief Main type 18 — a scene recall or store command.
    template <>
    struct traits<18u>
    {
        /// @brief The C++ type of the value.
        using value_t = scene_control;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(18u).value();

        /// @brief Encodes a scene command.
        /// @param value The command to encode.
        /// @return The encoded payload, or `error::value_out_of_range` when the scene number exceeds 63.
        [[nodiscard]] static constexpr encode_result_t encode(const scene_control value) noexcept
        {
            if (value.scene > 0x3Fu)
                return std::unexpected(error::value_out_of_range);

            return detail::write_unsigned<1u>(static_cast<std::uint8_t>((value.learn ? 0x80u : 0x00u) | value.scene));
        }

        /// @brief Decodes a scene command.
        /// @param value The received value.
        /// @return The decoded command, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<scene_control, error> decode(const value_view& value) noexcept
        {
            const auto raw = detail::read_unsigned<1u>(value);
            if (!raw.has_value())
                return std::unexpected(raw.error());

            return scene_control {.learn = (*raw & 0x80u) != 0u, .scene = static_cast<std::uint8_t>(*raw & 0x3Fu)};
        }
    };

    /// @brief Main type 20 — a one octet enumeration, such as an HVAC mode.
    template <>
    struct traits<20u>
    {
        /// @brief The C++ type of the value.
        using value_t = std::uint8_t;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(20u).value();

        /// @brief Encodes an enumeration value.
        /// @param value The value to encode.
        /// @return The encoded payload.
        [[nodiscard]] static constexpr encode_result_t encode(const std::uint8_t value) noexcept
        {
            return detail::write_unsigned<1u>(value);
        }

        /// @brief Decodes an enumeration value.
        /// @param value The received value.
        /// @return The decoded value, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<std::uint8_t, error> decode(const value_view& value) noexcept
        {
            const auto raw = detail::read_unsigned<1u>(value);
            if (!raw.has_value())
                return std::unexpected(raw.error());

            return static_cast<std::uint8_t>(*raw);
        }
    };

    /// @brief Main type 232 — an RGB colour.
    template <>
    struct traits<232u>
    {
        /// @brief The C++ type of the value.
        using value_t = rgb;
        /// @brief The descriptor of this main type.
        static constexpr descriptor info = describe(232u).value();

        /// @brief Encodes a colour.
        /// @param value The colour to encode.
        /// @return The encoded payload.
        [[nodiscard]] static constexpr encode_result_t encode(const rgb value) noexcept
        {
            const std::array<std::uint8_t, 3u> octets {value.red, value.green, value.blue};
            return payload::octets(octets);
        }

        /// @brief Decodes a colour.
        /// @param value The received value.
        /// @return The decoded colour, or the reason it could not be decoded.
        [[nodiscard]] static constexpr std::expected<rgb, error> decode(const value_view& value) noexcept
        {
            if (value.compacted || (value.octets.size() != 3u))
                return std::unexpected(error::unsupported_datapoint);

            return rgb {.red = value.octets[0u], .green = value.octets[1u], .blue = value.octets[2u]};
        }
    };
}
#endif // KMX_AIO_FEATURE_KNX
