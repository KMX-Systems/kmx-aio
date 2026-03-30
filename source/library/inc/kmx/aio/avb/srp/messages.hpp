/// @file avb/srp/messages.hpp
/// @brief MSRP (Multiple Stream Reservation Protocol, IEEE 802.1Qat) PDU wire formats.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <array>
#include <cstdint>

#include <kmx/aio/avb/avb_types.hpp>

namespace kmx::aio::avb::srp
{
    // MRP protocol constants

    /// @brief MSRP application address (AA) as per IEEE 802.1Qat Table 10-1.
    inline constexpr std::uint8_t mrp_protocol_version   = 0u;

    /// @brief MSRP attribute types.
    enum class attr_type : std::uint8_t
    {
        talker_advertise  = 0x01u,
        talker_failed     = 0x02u,
        listener          = 0x03u,
        domain            = 0x04u,
    };

    /// @brief Listener declaration subtypes (four-valued).
    enum class listener_decl : std::uint8_t
    {
        ignore       = 0x00u,
        asking_failed= 0x01u,
        ready        = 0x02u,
        ready_failed = 0x03u,
    };

    // Stream descriptor (shared across SRP and AVTP)

    /// @brief Fully describes an AVB stream for reservation and transport.
    struct stream_descriptor
    {
        stream_id_t    stream_id {};
        mac_address_t  dest_mac  {};          ///< Multicast destination MAC (L2)
        vlan_tag_t     vlan      {};
        std::uint16_t  max_frame_size  { 60u };   ///< Max AVTP frame payload bytes
        std::uint16_t  max_interval_frames { 1u }; ///< Frames per class measurement interval
        std::uint8_t   priority_and_rank { 0x60u }; ///< [7:5]=PCP 3 (ClassA), [0]=rank
        std::uint32_t  accumulated_latency { 0u };   ///< End-to-end latency (ns)
        std::uint32_t  frames_per_sec { 48000u };    ///< 48kHz sample rate default
    };

    // MRP PDU building blocks

#pragma pack(push, 1)

    /// @brief MRP message header (1 byte per attribute-type block).
    struct mrp_msg_header_t
    {
        std::uint8_t  attribute_type {};
        std::uint16_t attribute_list_length {};  ///< Big-endian
    };

    /// @brief MRP vector header (2 bytes preceding each vector).
    struct mrp_vector_header_t
    {
        std::uint16_t leave_all_and_num_values {}; ///< [15]=LeaveAll, [12:0]=NumberOfValues
    };

    /// @brief Talker Advertise attribute value (25 bytes).
    struct talker_advertise_attr_t
    {
        std::array<std::uint8_t, 8u> stream_id       {};  ///< source_mac + unique_id
        std::array<std::uint8_t, 6u> dest_mac         {};
        std::uint16_t                vlan_id           {};  ///< Big-endian
        std::uint16_t                max_frame_size    {};  ///< Big-endian
        std::uint16_t                max_interval_frames {};
        std::uint8_t                 priority_and_rank {};
        std::uint32_t                accumulated_latency {};
    };

    /// @brief Listener attribute value (8 bytes = stream_id only; subtype is threepacked).
    struct listener_attr_t
    {
        std::array<std::uint8_t, 8u> stream_id {};
    };

    /// @brief Domain attribute value (4 bytes).
    struct domain_attr_t
    {
        std::uint8_t  sr_class_id        {};
        std::uint8_t  sr_class_priority  {};
        std::uint16_t sr_class_vid       {};  ///< Big-endian VLAN ID
    };

    /// @brief Complete MSRP Talker Advertise PDU (minimal — one vector, one value).
    struct msrp_talker_pdu_t
    {
        std::uint8_t             protocol_version { mrp_protocol_version };
        mrp_msg_header_t         msg_header {};
        mrp_vector_header_t      vec_header {};
        talker_advertise_attr_t  attr_value {};
        std::uint8_t             three_packed_events { 0u };  ///< New=0
        std::array<std::uint8_t, 2u> end_mark { 0x00u, 0x00u };
    };

    /// @brief Complete MSRP Listener PDU (one vector, one value).
    struct msrp_listener_pdu_t
    {
        std::uint8_t             protocol_version { mrp_protocol_version };
        mrp_msg_header_t         msg_header {};
        mrp_vector_header_t      vec_header {};
        listener_attr_t          attr_value {};
        std::uint8_t             three_packed_events {};  ///< declaration subtype
        std::uint8_t             four_packed_events  { 0u };
        std::array<std::uint8_t, 2u> end_mark { 0x00u, 0x00u };
    };

    /// @brief Complete MSRP Domain PDU (mandatory, announces SRP class support).
    struct msrp_domain_pdu_t
    {
        std::uint8_t            protocol_version { mrp_protocol_version };
        mrp_msg_header_t        msg_header {};
        mrp_vector_header_t     vec_header {};
        domain_attr_t           attr_value {};
        std::uint8_t            three_packed_events { 0u };
        std::array<std::uint8_t, 2u> end_mark { 0x00u, 0x00u };
    };

#pragma pack(pop)

    // Encoding helpers

    /// @brief Pack stream_id into wire format (big-endian MAC + unique_id).
    inline std::array<std::uint8_t, 8u>
    encode_stream_id(const stream_id_t& sid) noexcept
    {
        std::array<std::uint8_t, 8u> out {};
        for (std::size_t i = 0; i < 6u; ++i)
            out[i] = sid.source_mac[i];
        out[6] = static_cast<std::uint8_t>(sid.unique_id >> 8u);
        out[7] = static_cast<std::uint8_t>(sid.unique_id & 0xFFu);
        return out;
    }

    /// @brief Build a Talker Advertise attribute from a stream_descriptor.
    inline talker_advertise_attr_t
    encode_talker(const stream_descriptor& desc) noexcept
    {
        talker_advertise_attr_t a {};
        a.stream_id             = encode_stream_id(desc.stream_id);
        a.dest_mac              = desc.dest_mac;
        a.vlan_id               = ::htons(desc.vlan.vid);
        a.max_frame_size        = ::htons(desc.max_frame_size);
        a.max_interval_frames   = ::htons(desc.max_interval_frames);
        a.priority_and_rank     = desc.priority_and_rank;
        a.accumulated_latency   = ::htonl(desc.accumulated_latency);
        return a;
    }

} // namespace kmx::aio::avb::srp
