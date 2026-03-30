/// @file avb/gptp/messages.hpp
/// @brief IEEE 802.1AS gPTP message structures (packed for direct wire encoding).
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include <arpa/inet.h>

#include <kmx/aio/avb/avb_types.hpp>

namespace kmx::aio::avb::gptp
{
    // Message types (IEEE 802.1AS Table 10-1)

    enum class msg_type : std::uint8_t
    {
        sync                  = 0x00,
        pdelay_req            = 0x02,
        pdelay_resp           = 0x03,
        follow_up             = 0x08,
        pdelay_resp_follow_up = 0x0A,
        announce              = 0x0B,
        signaling             = 0x0C,
        management            = 0x0D,
    };

    // Clock identity (64-bit)

    struct clock_identity_t
    {
        std::array<std::uint8_t, 8> id {};

        [[nodiscard]] bool operator==(const clock_identity_t&) const noexcept = default;
    };

    // Port identity = clockId + portNumber

    struct port_identity_t
    {
        clock_identity_t clock_id {};
        std::uint16_t    port_number {};

        [[nodiscard]] bool operator==(const port_identity_t&) const noexcept = default;
    };

    // Timestamp (10 bytes: seconds 48-bit + nanoseconds 32-bit)

    struct timestamp_t
    {
        std::array<std::uint8_t, 6> seconds_msb {};  ///< seconds[47:16]
        std::uint32_t               nanoseconds {};   ///< in network byte order

        /// @brief Convert to nanoseconds since epoch (host byte order).
        [[nodiscard]] avb_timestamp_t to_ns() const noexcept
        {
            std::uint64_t sec = 0;
            for (int i = 0; i < 6; ++i)
                sec = (sec << 8) | seconds_msb[static_cast<std::size_t>(i)];
            return sec * 1'000'000'000ULL + ::ntohl(nanoseconds);
        }

        static timestamp_t from_ns(avb_timestamp_t ns) noexcept
        {
            const std::uint64_t sec  = ns / 1'000'000'000ULL;
            const std::uint32_t nsec = static_cast<std::uint32_t>(ns % 1'000'000'000ULL);
            timestamp_t ts {};
            for (int i = 5; i >= 0; --i)
            {
                ts.seconds_msb[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(sec >> (8 * (5 - i)));
            }
            ts.nanoseconds = ::htonl(nsec);
            return ts;
        }
    };

    // Common gPTP header (34 bytes, packed)

#pragma pack(push, 1)
    struct header_t
    {
        std::uint8_t  transport_msg_type {};  ///< [7:4]=transportSpecific, [3:0]=messageType
        std::uint8_t  version_ptp { 0x02 };  ///< [7:4]=reserved, [3:0]=versionPTP=2
        std::uint16_t message_length {};      ///< total msg length, network byte order
        std::uint8_t  domain_number {};
        std::uint8_t  reserved1 {};
        std::uint16_t flags {};
        std::int64_t  correction_field {};    ///< ns * 2^16, network byte order
        std::uint32_t reserved2 {};
        port_identity_t source_port_id {};
        std::uint16_t sequence_id {};
        std::uint8_t  control {};
        std::int8_t   log_message_interval {};

        [[nodiscard]] msg_type type() const noexcept
        {
            return static_cast<msg_type>(transport_msg_type & 0x0F);
        }

        void set_type(msg_type t) noexcept
        {
            transport_msg_type = (transport_msg_type & 0xF0) | (static_cast<std::uint8_t>(t) & 0x0F);
        }
    };

    // Sync body (10 bytes — timestamp is zero for two-step)

    struct sync_body_t
    {
        timestamp_t origin_timestamp {};  ///< zero for two-step Sync
    };

    // Follow_Up body (10 bytes)

    struct follow_up_body_t
    {
        timestamp_t precise_origin_timestamp {};
    };

    // Pdelay_Req body (20 bytes)

    struct pdelay_req_body_t
    {
        timestamp_t    origin_timestamp {};
        port_identity_t reserved_port_id {};
    };

    // Pdelay_Resp body (20 bytes)

    struct pdelay_resp_body_t
    {
        timestamp_t     request_receipt_timestamp {};
        port_identity_t requesting_port_id {};
    };

    // Pdelay_Resp_Follow_Up body (20 bytes)

    struct pdelay_resp_follow_up_body_t
    {
        timestamp_t     response_origin_timestamp {};
        port_identity_t requesting_port_id {};
    };

    // Complete frame wrappers

    struct sync_frame_t
    {
        header_t    header {};
        sync_body_t body {};
    };

    struct follow_up_frame_t
    {
        header_t        header {};
        follow_up_body_t body {};
    };

    struct pdelay_req_frame_t
    {
        header_t          header {};
        pdelay_req_body_t body {};
    };

    struct pdelay_resp_frame_t
    {
        header_t           header {};
        pdelay_resp_body_t body {};
    };

    struct pdelay_resp_follow_up_frame_t
    {
        header_t                    header {};
        pdelay_resp_follow_up_body_t body {};
    };

#pragma pack(pop)

    /// @brief Compute port identity from a local MAC address (EUI-64 insertion).
    inline clock_identity_t mac_to_clock_id(const mac_address_t& mac) noexcept
    {
        clock_identity_t id {};
        // Insert 0xFF 0xFE in the middle per IEEE EUI-64
        id.id[0] = mac[0] ^ 0x02u;  // flip U/L bit
        id.id[1] = mac[1];
        id.id[2] = mac[2];
        id.id[3] = 0xFF;
        id.id[4] = 0xFE;
        id.id[5] = mac[3];
        id.id[6] = mac[4];
        id.id[7] = mac[5];
        return id;
    }

} // namespace kmx::aio::avb::gptp
