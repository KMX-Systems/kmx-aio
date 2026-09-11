/// @file api/kmx/aio/knx/cemi_bytes_storage.hpp
/// @brief Inline storage for the cEMI octets a decoded KNXnet/IP frame carries.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @reference KNX System Specifications, 03/08/04 "Tunnelling".
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/cemi.hpp>

        #include <algorithm>
        #include <array>
        #include <cstdint>
    #endif

namespace kmx::aio::knx
{
    /// @brief Inline storage for the cEMI octets of one decoded tunnelling request.
    /// @details Sized to @ref kmx::aio::knx::cemi::max_message_size, the largest message the cEMI decoder
    ///          can accept, rather than to `max_l_data_size`, which describes only what this build's
    ///          encoder emits and is 255 octets short of what a peer may legitimately send.
    struct cemi_bytes_storage
    {
        /// @brief The storage; only its first @ref size octets are meaningful.
        std::array<std::uint8_t, cemi::max_message_size> bytes {};
        /// @brief How many octets of @ref bytes the decoder filled in.
        std::uint16_t size {};

        /// @brief Indicates whether no octets are held.
        [[nodiscard]] constexpr bool empty() const noexcept { return size == 0u; }
        /// @brief Returns how many octets are held.
        [[nodiscard]] constexpr std::size_t length() const noexcept { return size; }
        /// @brief Returns an iterator to the first octet.
        [[nodiscard]] constexpr auto begin() const noexcept { return bytes.begin(); }
        /// @brief Returns an iterator one past the last meaningful octet.
        [[nodiscard]] constexpr auto end() const noexcept { return bytes.begin() + size; }
        /// @brief Returns a view of the meaningful octets.
        [[nodiscard]] constexpr cspan_uint8_t span() const noexcept { return {bytes.data(), size}; }

        /// @brief Compares the held octets against an owning buffer.
        /// @param lhs The inline storage.
        /// @param rhs The buffer to compare against.
        /// @return `true` when both hold the same octets.
        /// @note Compares only the meaningful prefix, not the unused tail of @ref bytes.
        [[nodiscard]] friend bool operator==(const cemi_bytes_storage& lhs, const byte_buffer_t& rhs) noexcept
        {
            return (lhs.span().size() == rhs.size()) && std::equal(lhs.span().begin(), lhs.span().end(), rhs.begin());
        }
    };
}
#endif // KMX_AIO_FEATURE_KNX
