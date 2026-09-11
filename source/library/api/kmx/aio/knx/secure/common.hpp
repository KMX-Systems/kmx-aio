/// @file api/kmx/aio/knx/secure/common.hpp
/// @brief Types shared by every KNX Secure profile: sequence fields, serial numbers, clocks and counters.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/knx/dib/device_info.hpp>

        #include <array>
        #include <cstdint>
    #endif

namespace kmx::aio::knx::secure
{
    /// @brief 64-bit monotonic milliseconds; null selects the steady clock.
    /// @note Not the 32-bit clock the rest of the KNX layer uses: a 32-bit millisecond count wraps after 49.7
    ///       days, and the secure routing timer is 48 bits wide and must never move backwards.
    using monotonic_ms_function = std::uint64_t (*)() noexcept;
    /// @brief Milliseconds since the Unix epoch; null selects the system clock.
    using wall_clock_ms_function = std::uint64_t (*)() noexcept;

    /// @brief A KNX serial number, the very type a DEVICE_INFO block carries.
    using serial_number_t = dib::serial_number_t;
    /// @brief The six-octet sequence information of a SECURE_WRAPPER, or a timer value.
    using sequence_information_t = std::array<std::uint8_t, 6u>;
    /// @brief The two-octet message tag of a SECURE_WRAPPER or TIMER_NOTIFY.
    using message_tag_t = std::array<std::uint8_t, 2u>;
    /// @brief Width of a KNX Secure message authentication code, in octets.
    inline constexpr std::size_t mac_size = 16u;
    /// @brief A message authentication code as it travels: encrypted.
    using mac_t = std::array<std::uint8_t, mac_size>;

    /// @brief Largest value a 48-bit sequence field holds.
    inline constexpr std::uint64_t max_sequence = (std::uint64_t {1u} << 48u) - 1u;

    /// @brief Encodes a 48-bit value big-endian.
    /// @param value The value; bits above the 48th are dropped.
    /// @return The six octets.
    [[nodiscard]] constexpr sequence_information_t encode_sequence(const std::uint64_t value) noexcept
    {
        sequence_information_t result {};
        for (std::size_t index {}; index < result.size(); ++index)
            result[index] = static_cast<std::uint8_t>((value >> (8u * (result.size() - 1u - index))) & 0xFFu);
        return result;
    }

    /// @brief Decodes a 48-bit big-endian value.
    /// @param octets The six octets.
    /// @return The value.
    [[nodiscard]] constexpr std::uint64_t decode_sequence(const sequence_information_t& octets) noexcept
    {
        std::uint64_t result {};
        for (const auto octet: octets)
            result = (result << 8u) | octet;
        return result;
    }

    /// @brief Indicates whether a serial number is usable by a secure endpoint.
    /// @details All zero is refused. A serial number is what tells one sender's frames from another's, and a
    ///          default every instance shares - the zero of an unset field - makes two endpoints indistinguishable.
    [[nodiscard]] constexpr bool valid_serial_number(const serial_number_t& value) noexcept
    {
        for (const auto octet: value)
            if (octet != 0u)
                return true;
        return false;
    }

    /// @brief What a secure endpoint has refused, and what it has done, since it was created.
    /// @details Never reset. A deployment has no other evidence that someone is probing its keys: a rising
    ///          authentication failure count is the signal.
    struct statistics
    {
        /// @brief Frames whose message authentication code did not verify.
        std::uint64_t authentication_failures {};
        /// @brief Authenticated frames refused as replayed, as outside their acceptance window, or - for routing -
        ///        as arriving before the timer synchronised.
        std::uint64_t replays {};
        /// @brief Exact repeats of an already accepted frame.
        std::uint64_t duplicates {};
        /// @brief Frames for which no key was configured, or whose sender is not allowed.
        std::uint64_t missing_keys {};
        /// @brief Unencrypted frames refused because the service must be secured.
        std::uint64_t unencrypted_refused {};
        /// @brief Authenticated frames refused for the service they carry: one that may not be wrapped, or one the
        ///        endpoint does not take in a wrapper.
        std::uint64_t refused_services {};
        /// @brief Secure sessions established.
        std::uint64_t sessions_opened {};
        /// @brief Secure sessions closed, by either side.
        std::uint64_t sessions_closed {};
        /// @brief Secure sessions that timed out.
        std::uint64_t sessions_timed_out {};
        /// @brief TIMER_NOTIFY frames sent.
        std::uint64_t timer_notifications_sent {};
        /// @brief Times the routing timer offset moved forward to a newer authenticated value.
        std::uint64_t timer_adjustments {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
