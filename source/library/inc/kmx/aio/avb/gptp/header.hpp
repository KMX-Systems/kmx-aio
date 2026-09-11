/// @file inc/kmx/aio/avb/gptp/header.hpp
/// @brief The common IEEE 802.1AS gPTP message header, with the message type and identities it carries.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstdint>
#endif

namespace kmx::aio::avb::gptp
{
    /// @brief gPTP message type carried in the low nibble of the header's first octet.
    /// @reference IEEE 802.1AS Table 10-1.
    enum class msg_type : std::uint8_t
    {
        sync = 0x00u,                  ///< Two-step Sync message; carries no usable timestamp.
        pdelay_req = 0x02u,            ///< Peer delay request initiating a path-delay measurement.
        pdelay_resp = 0x03u,           ///< Peer delay response echoing the request receipt time.
        follow_up = 0x08u,             ///< Follow_Up carrying the precise origin timestamp of a Sync.
        pdelay_resp_follow_up = 0x0Au, ///< Peer delay response follow-up carrying the precise response time.
        announce = 0x0Bu,              ///< Announce message advertising the grandmaster and its properties.
        signaling = 0x0Cu,             ///< Signaling message negotiating message intervals.
        management = 0x0Du,            ///< Management message.
    };

    /// @brief 64-bit gPTP clock identity uniquely naming a time-aware system.
    struct clock_identity_t
    {
        /// @brief The eight identity octets, in wire order.
        std::array<std::uint8_t, 8u> id {};

        /// @brief Compares two clock identities octet by octet.
        /// @return `true` when both identities are equal.
        [[nodiscard]] bool operator==(const clock_identity_t&) const noexcept = default;
    };

    /// @brief gPTP port identity: a clock identity plus the port number within that clock.
    struct port_identity_t
    {
        /// @brief The identity of the clock owning the port.
        clock_identity_t clock_id {};
        /// @brief The 1-based port number within the clock, in network byte order.
        std::uint16_t port_number {};

        /// @brief Compares clock identity and port number.
        /// @return `true` when both port identities are equal.
        [[nodiscard]] bool operator==(const port_identity_t&) const noexcept = default;
    };

#pragma pack(push, 1)
    /// @brief Common 34-byte gPTP message header preceding every message body.
    struct header
    {
        std::uint8_t transport_msg_type {};  ///< [7:4]=transportSpecific, [3:0]=messageType
        std::uint8_t version_ptp {2u};       ///< [7:4]=reserved, [3:0]=versionPTP=2
        std::uint16_t message_length {};     ///< total msg length, network byte order
        std::uint8_t domain_number {};       ///< gPTP domain this message belongs to
        std::uint8_t reserved1 {};           ///< reserved, transmitted as zero
        std::uint16_t flags {};              ///< message flags, network byte order
        std::int64_t correction_field {};    ///< ns * 2^16, network byte order
        std::uint32_t reserved2 {};          ///< reserved, transmitted as zero
        port_identity_t source_port_id {};   ///< identity of the port that sent the message
        std::uint16_t sequence_id {};        ///< per-message-type sequence counter, network byte order
        std::uint8_t control {};             ///< legacy PTPv1 control field
        std::int8_t log_message_interval {}; ///< log2 of the mean interval between messages of this type

        /// @brief Extracts the message type from the low nibble of the first octet.
        /// @return The decoded message type.
        [[nodiscard]] msg_type type() const noexcept { return static_cast<msg_type>(transport_msg_type & 0x0F); }

        /// @brief Stores a message type into the low nibble, preserving the transportSpecific nibble.
        /// @param t The message type to encode.
        void set_type(const msg_type t) noexcept
        {
            transport_msg_type = (transport_msg_type & 0xF0) | (static_cast<std::uint8_t>(t) & 0x0Fu);
        }
    };

#pragma pack(pop)
}
