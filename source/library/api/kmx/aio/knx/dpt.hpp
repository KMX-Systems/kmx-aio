/// @file aio/knx/dpt.hpp
/// @brief KNX datapoint type encoding and decoding.
/// @details
/// A KNX group telegram carries a bare value whose meaning comes entirely from the datapoint type the
/// installation assigned to the group address. This header turns that value between its wire form and an
/// ordinary C++ type for the datapoint main types that real installations actually use.
///
/// A datapoint type is written `main.sub`. The main type fixes the wire format — how many bits, how they
/// are laid out, how a number is scaled — and the sub type fixes only the unit and the range, so the
/// traits below are keyed on the main type and the sub types that need a different range appear as named
/// helpers beside them.
///
/// Values narrower than seven bits are not sent as octets at all: they ride in the six spare bits of the
/// APCI octet. @ref kmx::aio::knx::dpt::payload carries that distinction so it never has to be inferred
/// from a value's width.
///
/// Everything here is `constexpr` and allocation-free.
/// @reference KNX System Specifications, Volume 3/7/2 "Datapoint Types".
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <array>
        #include <bit>
        #include <compare>
        #include <cstdint>
        #include <expected>
        #include <span>
        #include <string_view>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/cemi.hpp>
    #include <kmx/aio/knx/error.hpp>

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
    /// @note This is the single source of truth for widths and names. Every specialisation below takes its
    ///       descriptor straight from this table through @ref describe, so the two cannot drift apart, and
    ///       a specialisation for a main type missing from the table fails to compile.
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

    /// @brief An encoded datapoint value, owning its octets.
    /// @details Fourteen octets is the widest value any implemented main type produces, which is the
    ///          character string of DPT 16. Keeping the storage inline means encoding a telegram allocates
    ///          nothing at all.
    class payload
    {
    public:
        /// @brief The largest number of octets an encoded value occupies.
        static constexpr std::size_t capacity = 14u;

        /// @brief Creates the compact value zero.
        constexpr payload() noexcept = default;

        /// @brief Creates a value that rides in the six spare bits of the APCI octet.
        /// @param value The six-bit value; higher bits are discarded.
        /// @return The payload.
        [[nodiscard]] static constexpr payload compact(const std::uint8_t value) noexcept
        {
            payload result {};
            result.compact_value_ = static_cast<std::uint8_t>(value & apdu_payload::compact_mask);
            return result;
        }

        /// @brief Creates a value carried in whole octets.
        /// @param octets The octets to copy in.
        /// @return The payload, or `error::payload_too_large` when it exceeds @ref capacity.
        [[nodiscard]] static constexpr std::expected<payload, error> octets(const cspan_uint8_t octets) noexcept
        {
            if (octets.size() > capacity)
                return std::unexpected(error::payload_too_large);

            payload result {};
            for (std::size_t i {}; i < octets.size(); ++i)
                result.storage_[i] = octets[i];

            result.size_ = static_cast<std::uint8_t>(octets.size());
            result.compacted_ = false;
            return result;
        }

        /// @brief Indicates whether the value rides in the six spare bits of the APCI octet.
        [[nodiscard]] constexpr bool compacted() const noexcept { return compacted_; }
        /// @brief Returns the six-bit value; zero unless the payload is compact.
        [[nodiscard]] constexpr std::uint8_t compact_value() const noexcept { return compact_value_; }
        /// @brief Returns the payload octets; empty when the payload is compact.
        [[nodiscard]] constexpr cspan_uint8_t view() const noexcept { return {storage_.data(), size_}; }

        /// @brief Returns the payload as an APDU payload the cEMI encoder accepts.
        /// @return A view of this object's storage.
        /// @warning The returned view borrows this object's octets, so this object must outlive it. The
        ///          rvalue overload is deleted to keep that mistake from compiling.
        [[nodiscard]] constexpr apdu_payload apdu() const& noexcept
        {
            if (compacted_)
                return apdu_payload::compact(compact_value_);

            return apdu_payload::extended(view()).value_or(apdu_payload {});
        }

        /// @brief Deleted so an APDU view cannot outlive a temporary payload.
        apdu_payload apdu() const&& = delete;

        /// @brief Compares two payloads by form and content.
        [[nodiscard]] constexpr bool operator==(const payload& other) const noexcept
        {
            if ((compacted_ != other.compacted_) || (compact_value_ != other.compact_value_) || (size_ != other.size_))
                return false;

            for (std::uint8_t i {}; i < size_; ++i)
                if (storage_[i] != other.storage_[i])
                    return false;

            return true;
        }

    private:
        /// @brief The value octets of a non-compact payload.
        std::array<std::uint8_t, capacity> storage_ {};
        /// @brief The number of value octets in use.
        std::uint8_t size_ {};
        /// @brief The six-bit value of a compact payload.
        std::uint8_t compact_value_ {};
        /// @brief Whether the value rides in the APCI octet.
        bool compacted_ {true};
    };

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
        [[nodiscard]] static constexpr encode_result_t encode(const bool value) noexcept
        {
            return payload::compact(value ? 1u : 0u);
        }

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
    [[nodiscard]] constexpr decode_result_t<Main> decode(const cemi_frame& frame,
                                                                                        const cspan_uint8_t bytes) noexcept
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
