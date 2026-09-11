/// @file inc/kmx/aio/avb/gptp/messages.hpp
/// @brief IEEE 802.1AS gPTP message structures (packed for direct wire encoding).
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/avb/avb_types.hpp>
    #include <kmx/aio/avb/gptp/header.hpp>
    #include <kmx/aio/avb/gptp/timestamp.hpp>
#endif

namespace kmx::aio::avb::gptp
{
#pragma pack(push, 1)
    /// @brief Sync message body (10 bytes).
    struct sync_body_t
    {
        timestamp origin_timestamp {}; ///< zero for two-step Sync
    };

    /// @brief Follow_Up message body (10 bytes).
    struct follow_up_body_t
    {
        /// @brief The precise egress time of the Sync this message follows up.
        timestamp precise_origin_timestamp {};
    };

    /// @brief Pdelay_Req message body (20 bytes).
    struct pdelay_req_body_t
    {
        /// @brief Reserved origin timestamp; transmitted as zero.
        timestamp origin_timestamp {};
        /// @brief Reserved port identity; transmitted as zero.
        port_identity_t reserved_port_id {};
    };

    /// @brief Pdelay_Resp message body (20 bytes).
    struct pdelay_resp_body_t
    {
        /// @brief The time at which the responder received the matching Pdelay_Req.
        timestamp request_receipt_timestamp {};
        /// @brief The port identity of the requester being answered.
        port_identity_t requesting_port_id {};
    };

    /// @brief Pdelay_Resp_Follow_Up message body (20 bytes).
    struct pdelay_resp_follow_up_body_t
    {
        /// @brief The precise egress time of the matching Pdelay_Resp.
        timestamp response_origin_timestamp {};
        /// @brief The port identity of the requester being answered.
        port_identity_t requesting_port_id {};
    };

    // A member named `header` would change the meaning of the type name `header` inside the struct, so the type is qualified.

    /// @brief Complete Sync frame: common header followed by the Sync body.
    struct sync_frame_t
    {
        gptp::header header {}; ///< common gPTP header
        sync_body_t body {};    ///< Sync payload
    };

    /// @brief Complete Follow_Up frame: common header followed by the Follow_Up body.
    struct follow_up_frame_t
    {
        gptp::header header {};   ///< common gPTP header
        follow_up_body_t body {}; ///< Follow_Up payload
    };

    /// @brief Complete Pdelay_Req frame: common header followed by the Pdelay_Req body.
    struct pdelay_req_frame_t
    {
        gptp::header header {};    ///< common gPTP header
        pdelay_req_body_t body {}; ///< Pdelay_Req payload
    };

    /// @brief Complete Pdelay_Resp frame: common header followed by the Pdelay_Resp body.
    struct pdelay_resp_frame_t
    {
        gptp::header header {};     ///< common gPTP header
        pdelay_resp_body_t body {}; ///< Pdelay_Resp payload
    };

    /// @brief Complete Pdelay_Resp_Follow_Up frame: common header followed by its body.
    struct pdelay_resp_follow_up_frame_t
    {
        gptp::header header {};               ///< common gPTP header
        pdelay_resp_follow_up_body_t body {}; ///< Pdelay_Resp_Follow_Up payload
    };

#pragma pack(pop)

    /// @brief Compute port identity from a local MAC address (EUI-64 insertion).
    /// @param mac The interface MAC address to derive the identity from.
    /// @return The derived clock identity, with the U/L bit flipped and `FF:FE` inserted.
    [[nodiscard]] clock_identity_t mac_to_clock_id(const mac_address_t& mac) noexcept;

}
