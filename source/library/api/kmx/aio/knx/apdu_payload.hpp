/// @file api/kmx/aio/knx/apdu_payload.hpp
/// @brief The value an application protocol data unit carries, compact or in whole octets.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @reference KNX System Specifications, Volume 3/3/7 "Application Layer", APCI codes.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/error.hpp>

        #include <cstdint>
        #include <expected>
        #include <span>
    #endif

namespace kmx::aio::knx
{
    /// @brief An application protocol data unit payload.
    /// @details A KNX APDU carries its value either in the six spare bits of the APCI octet — the compact
    ///          form every one-bit and four-bit datapoint uses — or in whole octets after it. The two are
    ///          different encodings of the same field, and which one applies follows from the datapoint
    ///          type rather than from the value, so the choice is made here and never guessed at.
    /// @warning The extended form is a view. The octets it names must outlive every encode call that uses
    ///          it; nothing is copied.
    class apdu_payload
    {
    public:
        /// @brief Largest number of payload octets an APDU can carry.
        /// @details The data length field is one octet and counts the APDU minus its first octet.
        static constexpr std::size_t max_octets = 254u;
        /// @brief Mask of the bits a compact payload occupies in the APCI octet.
        static constexpr std::uint8_t compact_mask = 0x3Fu;

        /// @brief Creates the compact payload with value zero, as used by A_GroupValue_Read.
        constexpr apdu_payload() noexcept = default;

        /// @brief Creates a compact payload carried inside the APCI octet.
        /// @param value The six-bit value; higher bits are discarded.
        /// @return The payload.
        [[nodiscard]] static constexpr apdu_payload compact(const std::uint8_t value) noexcept
        {
            apdu_payload result {};
            result.compact_value_ = static_cast<std::uint8_t>(value & compact_mask);
            return result;
        }

        /// @brief Creates a payload carried in whole octets after the APCI octet.
        /// @param octets The payload octets; borrowed, not copied.
        /// @return The payload, or `error::payload_too_large` when it exceeds @ref max_octets.
        /// @note An empty octet span yields the compact payload with value zero, because an APDU always
        ///       carries at least the APCI octet and therefore has no zero-length form.
        [[nodiscard]] static constexpr std::expected<apdu_payload, error> extended(const cspan_uint8_t octets) noexcept
        {
            if (octets.size() > max_octets)
                return std::unexpected(error::payload_too_large);
            if (octets.empty())
                return apdu_payload {};

            apdu_payload result {};
            result.octets_ = octets;
            result.compacted_ = false;
            return result;
        }

        /// @brief Indicates whether the value sits in the six spare bits of the APCI octet.
        [[nodiscard]] constexpr bool compacted() const noexcept { return compacted_; }
        /// @brief Returns the six-bit value; zero unless the payload is compact.
        [[nodiscard]] constexpr std::uint8_t compact_value() const noexcept { return compact_value_; }
        /// @brief Returns the payload octets; empty unless the payload is extended.
        [[nodiscard]] constexpr cspan_uint8_t octets() const noexcept { return octets_; }

        /// @brief Returns the wire data length field: the APDU octet count minus one.
        [[nodiscard]] constexpr std::uint8_t data_length() const noexcept
        {
            return compacted_ ? std::uint8_t {1u} : static_cast<std::uint8_t>(octets_.size() + 1u);
        }

    private:
        /// @brief The borrowed payload octets of an extended payload.
        cspan_uint8_t octets_ {};
        /// @brief The six-bit value of a compact payload.
        std::uint8_t compact_value_ {};
        /// @brief Whether the value sits in the APCI octet.
        bool compacted_ {true};
    };
}
#endif // KMX_AIO_FEATURE_KNX
