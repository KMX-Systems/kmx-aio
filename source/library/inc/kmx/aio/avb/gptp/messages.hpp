/// @file avb/gptp/messages.hpp
/// @brief IEEE 802.1AS gPTP message structures (packed for direct wire encoding).
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <arpa/inet.h>
    #include <array>
    #include <cstdint>
    #include <cstring>

    #include <kmx/aio/avb/avb_types.hpp>
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

    /// @brief gPTP wire timestamp: a 48-bit seconds field followed by a 32-bit nanoseconds field.
    struct timestamp_t
    {
        std::array<std::uint8_t, 6u> seconds_msb {}; ///< seconds[47:16]
        std::uint32_t nanoseconds {};                ///< in network byte order

        /// @brief Convert to nanoseconds since epoch (host byte order).
        /// @return The timestamp expressed as nanoseconds since the PTP epoch.
        [[nodiscard]] avb_timestamp_t to_ns() const noexcept
        {
            std::uint64_t sec = 0;
            for (int i = 0; i < 6; ++i)
                sec = (sec << 8u) | seconds_msb[static_cast<std::size_t>(i)];
            return sec * 1'000'000'000ULL + ::ntohl(nanoseconds);
        }

        /// @brief Builds a wire timestamp from nanoseconds since epoch.
        /// @param ns Nanoseconds since the PTP epoch, in host byte order.
        /// @return The equivalent wire-encoded timestamp.
        static timestamp_t from_ns(avb_timestamp_t ns) noexcept
        {
            const std::uint64_t sec = ns / 1'000'000'000ULL;
            const std::uint32_t nsec = static_cast<std::uint32_t>(ns % 1'000'000'000ULL);
            timestamp_t ts {};
            for (int i = 5; i >= 0; --i)
                ts.seconds_msb[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(sec >> (8 * (5 - i)));

            ts.nanoseconds = ::htonl(nsec);
            return ts;
        }
    };

#pragma pack(push, 1)
    /// @brief Common 34-byte gPTP message header preceding every message body.
    struct header_t
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

    /// @brief Sync message body (10 bytes).
    struct sync_body_t
    {
        timestamp_t origin_timestamp {}; ///< zero for two-step Sync
    };

    /// @brief Follow_Up message body (10 bytes).
    struct follow_up_body_t
    {
        /// @brief The precise egress time of the Sync this message follows up.
        timestamp_t precise_origin_timestamp {};
    };

    /// @brief Pdelay_Req message body (20 bytes).
    struct pdelay_req_body_t
    {
        /// @brief Reserved origin timestamp; transmitted as zero.
        timestamp_t origin_timestamp {};
        /// @brief Reserved port identity; transmitted as zero.
        port_identity_t reserved_port_id {};
    };

    /// @brief Pdelay_Resp message body (20 bytes).
    struct pdelay_resp_body_t
    {
        /// @brief The time at which the responder received the matching Pdelay_Req.
        timestamp_t request_receipt_timestamp {};
        /// @brief The port identity of the requester being answered.
        port_identity_t requesting_port_id {};
    };

    /// @brief Pdelay_Resp_Follow_Up message body (20 bytes).
    struct pdelay_resp_follow_up_body_t
    {
        /// @brief The precise egress time of the matching Pdelay_Resp.
        timestamp_t response_origin_timestamp {};
        /// @brief The port identity of the requester being answered.
        port_identity_t requesting_port_id {};
    };

    /// @brief Complete Sync frame: common header followed by the Sync body.
    struct sync_frame_t
    {
        header_t header {};  ///< common gPTP header
        sync_body_t body {}; ///< Sync payload
    };

    /// @brief Complete Follow_Up frame: common header followed by the Follow_Up body.
    struct follow_up_frame_t
    {
        header_t header {};       ///< common gPTP header
        follow_up_body_t body {}; ///< Follow_Up payload
    };

    /// @brief Complete Pdelay_Req frame: common header followed by the Pdelay_Req body.
    struct pdelay_req_frame_t
    {
        header_t header {};        ///< common gPTP header
        pdelay_req_body_t body {}; ///< Pdelay_Req payload
    };

    /// @brief Complete Pdelay_Resp frame: common header followed by the Pdelay_Resp body.
    struct pdelay_resp_frame_t
    {
        header_t header {};         ///< common gPTP header
        pdelay_resp_body_t body {}; ///< Pdelay_Resp payload
    };

    /// @brief Complete Pdelay_Resp_Follow_Up frame: common header followed by its body.
    struct pdelay_resp_follow_up_frame_t
    {
        header_t header {};                   ///< common gPTP header
        pdelay_resp_follow_up_body_t body {}; ///< Pdelay_Resp_Follow_Up payload
    };

#pragma pack(pop)

    /// @brief Compute port identity from a local MAC address (EUI-64 insertion).
    /// @param mac The interface MAC address to derive the identity from.
    /// @return The derived clock identity, with the U/L bit flipped and `FF:FE` inserted.
    inline clock_identity_t mac_to_clock_id(const mac_address_t& mac) noexcept
    {
        clock_identity_t id {};
        // Insert 0xFF 0xFE in the middle per IEEE EUI-64
        id.id[0u] = mac[0u] ^ 0x02u; // flip U/L bit
        id.id[1u] = mac[1u];
        id.id[2u] = mac[2u];
        id.id[3u] = 0xFFu;
        id.id[4u] = 0xFEu;
        id.id[5u] = mac[3u];
        id.id[6u] = mac[4u];
        id.id[7u] = mac[5u];
        return id;
    }

} // namespace kmx::aio::avb::gptp
