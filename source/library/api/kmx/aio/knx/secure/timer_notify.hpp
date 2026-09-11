/// @file aio/knx/secure/timer_notify.hpp
/// @brief TIMER_NOTIFY, the frame secure routers keep their timers aligned with.
/// @details
/// A TIMER_NOTIFY carries a timer value, a serial number, a message tag and a MAC under the backbone key, and
/// nothing encrypted. The MAC's B0 is the timer value, serial number and message tag followed by a zero length,
/// its associated data is the KNXnet/IP header, and the first counter block encrypts it.
///
/// The serial number and tag are not always the sender's own. A router that receives an outdated frame answers
/// with an update notify carrying the outdated sender's serial number and message tag, so that sender can tell
/// the update is meant for it, and the answer to a synchronisation request echoes the requester's the same way.
/// @reference KNX AN159 v06 §2.2.2.3; xknx 3.20.0 `SecureSequenceTimer`.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <cstddef>
        #include <cstdint>
        #include <expected>
        #include <system_error>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/frame.hpp>
    #include <kmx/aio/knx/secure/common.hpp>
    #include <kmx/aio/knx/secure/key.hpp>

namespace kmx::aio::knx::secure
{
    /// @brief Service type of a TIMER_NOTIFY.
    inline constexpr std::uint16_t timer_notify_service = 0x0955u;
    /// @brief Body size: timer value 6, serial number 6, message tag 2, MAC 16.
    inline constexpr std::size_t timer_notify_body_size = 30u;
    /// @brief Total size, header included.
    inline constexpr std::size_t timer_notify_size = frame::communication_header_size + timer_notify_body_size;

    /// @brief One TIMER_NOTIFY.
    struct timer_notify_frame
    {
        /// @brief The timer value, 48 bits big-endian.
        sequence_information_t timer_value {};
        /// @brief The serial number: the sender's, or the one of the router an update is addressed to.
        serial_number_t serial_number {};
        /// @brief The message tag.
        message_tag_t message_tag {};
        /// @brief The encrypted MAC.
        mac_t mac {};
    };

    /// @brief A TIMER_NOTIFY, or why none was decoded or built.
    using timer_notify_result_t = std::expected<timer_notify_frame, std::error_code>;

    /// @brief Decodes a TIMER_NOTIFY.
    /// @param packet The received octets, header included.
    /// @return The frame.
    /// @retval kmx::aio::knx::error::unsupported_service The datagram is another service.
    /// @retval kmx::aio::knx::error::malformed_frame The datagram is not exactly @ref timer_notify_size octets.
    [[nodiscard]] timer_notify_result_t decode_timer_notify_packet(cspan_uint8_t packet) noexcept;

    /// @brief Encodes a TIMER_NOTIFY.
    /// @param destination The destination octets.
    /// @param value The frame, MAC included.
    /// @return Nothing, or @ref kmx::aio::knx::error::invalid_length.
    [[nodiscard]] expected_void_t encode_timer_notify_packet(span_uint8_t destination, const timer_notify_frame& value) noexcept;

    /// @brief Builds an authenticated TIMER_NOTIFY.
    /// @param backbone_key The backbone key.
    /// @param timer_value The timer value; at most @ref max_sequence.
    /// @param serial_number The serial number to carry.
    /// @param message_tag The message tag to carry.
    /// @return The frame, MAC included.
    /// @retval kmx::aio::knx::error::invalid_configuration The timer value does not fit 48 bits.
    /// @retval kmx::aio::knx::error::crypto_failure The backend failed.
    [[nodiscard]] timer_notify_result_t make_timer_notify(const secret_key& backbone_key, std::uint64_t timer_value,
                                                          const serial_number_t& serial_number, const message_tag_t& message_tag) noexcept;

    /// @brief Verifies a TIMER_NOTIFY's MAC.
    /// @param backbone_key The backbone key.
    /// @param value The received frame.
    /// @return Nothing when the frame is authentic.
    /// @retval kmx::aio::knx::error::secure_authentication_failed The MAC does not verify.
    /// @retval kmx::aio::knx::error::crypto_failure The backend failed.
    [[nodiscard]] expected_void_t verify_timer_notify(const secret_key& backbone_key, const timer_notify_frame& value) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
