/// @file avb/srp/messages.hpp
/// @brief MSRP (Multiple Stream Reservation Protocol, IEEE 802.1Qat) PDU wire formats.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstdint>

    #include <kmx/aio/avb/avb_types.hpp>
#endif

namespace kmx::aio::avb::srp
{
    // MRP protocol constants

    /// @brief MSRP application address (AA) as per IEEE 802.1Qat Table 10-1.
    inline constexpr std::uint8_t mrp_protocol_version = 0u;

    /// @brief MSRP attribute types.
    enum class attr_type : std::uint8_t
    {
        /// @brief Talker advertising a stream it is able to transmit.
        talker_advertise = 0x01u,
        /// @brief Talker advertising a stream whose reservation has failed along the path.
        talker_failed = 0x02u,
        /// @brief Listener declaring interest in a stream.
        listener = 0x03u,
        /// @brief Domain attribute announcing the SR class this port supports.
        domain = 0x04u,
    };

    /// @brief Listener declaration subtypes (four-valued).
    enum class listener_decl : std::uint8_t
    {
        /// @brief No declaration; the listener is not interested in the stream.
        ignore = 0x00u,
        /// @brief The listener wants the stream but bandwidth could not be reserved.
        asking_failed = 0x01u,
        /// @brief The stream is reserved end to end and ready to receive.
        ready = 0x02u,
        /// @brief Some listeners are ready while others failed to reserve.
        ready_failed = 0x03u,
    };

    // Stream descriptor (shared across SRP and AVTP)

    /// @brief Fully describes an AVB stream for reservation and transport.
    struct stream_descriptor
    {
        /// @brief The 64-bit stream identifier (talker MAC plus unique id).
        stream_id_t stream_id {};
        mac_address_t dest_mac {}; ///< Multicast destination MAC (L2)
        /// @brief VLAN tag carrying the SR class priority and VLAN id.
        vlan_tag_t vlan {};
        std::uint16_t max_frame_size {60u};     ///< Max AVTP frame payload bytes
        std::uint16_t max_interval_frames {1u}; ///< Frames per class measurement interval
        std::uint8_t priority_and_rank {0x60u}; ///< [7:5]=PCP 3 (ClassA), [0]=rank
        std::uint32_t accumulated_latency {};   ///< End-to-end latency (ns)
        std::uint32_t frames_per_sec {48000u};  ///< 48kHz sample rate default
    };

    // MRP PDU building blocks

#pragma pack(push, 1)

    /// @brief MRP message header (1 byte per attribute-type block).
    struct mrp_msg_header_t
    {
        /// @brief The @ref attr_type value the following attribute list holds.
        std::uint8_t attribute_type {};
        std::uint16_t attribute_list_length {}; ///< Big-endian
    };

    /// @brief MRP vector header (2 bytes preceding each vector).
    struct mrp_vector_header_t
    {
        std::uint16_t leave_all_and_num_values {}; ///< [15]=LeaveAll, [12:0]=NumberOfValues
    };

    /// @brief Talker Advertise attribute value (25 bytes).
    struct talker_advertise_attr_t
    {
        /// @brief The stream identifier, in wire order.
        std::array<std::uint8_t, 8u> stream_id {}; ///< source_mac + unique_id
        /// @brief The multicast destination MAC of the stream, in wire order.
        std::array<std::uint8_t, 6u> dest_mac {};
        std::uint16_t vlan_id {};        ///< Big-endian
        std::uint16_t max_frame_size {}; ///< Big-endian
        /// @brief Frames per class measurement interval, big-endian.
        std::uint16_t max_interval_frames {};
        /// @brief [7:5]=PCP, [4:1]=reserved, [0]=rank.
        std::uint8_t priority_and_rank {};
        /// @brief Accumulated end-to-end latency in nanoseconds, big-endian.
        std::uint32_t accumulated_latency {};
    };

    /// @brief Listener attribute value (8 bytes = stream_id only; subtype is threepacked).
    struct listener_attr_t
    {
        /// @brief The stream identifier being declared, in wire order.
        std::array<std::uint8_t, 8u> stream_id {};
    };

    /// @brief Domain attribute value (4 bytes).
    struct domain_attr_t
    {
        /// @brief The SR class identifier (0 = class A, 1 = class B).
        std::uint8_t sr_class_id {};
        /// @brief The VLAN priority (PCP) assigned to the SR class.
        std::uint8_t sr_class_priority {};
        std::uint16_t sr_class_vid {}; ///< Big-endian VLAN ID
    };

    /// @brief Complete MSRP Talker Advertise PDU (minimal — one vector, one value).
    struct msrp_talker_pdu_t
    {
        /// @brief MRP protocol version; always @ref mrp_protocol_version.
        std::uint8_t protocol_version {mrp_protocol_version};
        /// @brief Attribute-type block header.
        mrp_msg_header_t msg_header {};
        /// @brief Vector header describing the single value that follows.
        mrp_vector_header_t vec_header {};
        /// @brief The Talker Advertise attribute value.
        talker_advertise_attr_t attr_value {};
        std::uint8_t three_packed_events {}; ///< New=0
        /// @brief Two zero octets terminating the attribute list.
        std::array<std::uint8_t, 2u> end_mark {0x00u, 0x00u};
    };

    /// @brief Complete MSRP Listener PDU (one vector, one value).
    struct msrp_listener_pdu_t
    {
        /// @brief MRP protocol version; always @ref mrp_protocol_version.
        std::uint8_t protocol_version {mrp_protocol_version};
        /// @brief Attribute-type block header.
        mrp_msg_header_t msg_header {};
        /// @brief Vector header describing the single value that follows.
        mrp_vector_header_t vec_header {};
        /// @brief The Listener attribute value.
        listener_attr_t attr_value {};
        std::uint8_t three_packed_events {}; ///< declaration subtype
        /// @brief Four-packed event byte carrying the @ref listener_decl subtype.
        std::uint8_t four_packed_events {};
        /// @brief Two zero octets terminating the attribute list.
        std::array<std::uint8_t, 2u> end_mark {0x00u, 0x00u};
    };

    /// @brief Complete MSRP Domain PDU (mandatory, announces SRP class support).
    struct msrp_domain_pdu_t
    {
        /// @brief MRP protocol version; always @ref mrp_protocol_version.
        std::uint8_t protocol_version {mrp_protocol_version};
        /// @brief Attribute-type block header.
        mrp_msg_header_t msg_header {};
        /// @brief Vector header describing the single value that follows.
        mrp_vector_header_t vec_header {};
        /// @brief The Domain attribute value.
        domain_attr_t attr_value {};
        /// @brief Three-packed event byte; New=0.
        std::uint8_t three_packed_events {};
        /// @brief Two zero octets terminating the attribute list.
        std::array<std::uint8_t, 2u> end_mark {0x00u, 0x00u};
    };

#pragma pack(pop)

    // Encoding helpers

    /// @brief Pack stream_id into wire format (big-endian MAC + unique_id).
    /// @param sid The stream identifier to encode.
    /// @return The eight-octet wire representation of @p sid.
    [[nodiscard]] std::array<std::uint8_t, 8u> encode_stream_id(const stream_id_t& sid) noexcept;

    /// @brief Build a Talker Advertise attribute from a stream_descriptor.
    [[nodiscard]] talker_advertise_attr_t encode_talker(const stream_descriptor& desc) noexcept;
    /// @param desc The stream description to encode.
    /// @return The wire-encoded Talker Advertise attribute.

} // namespace kmx::aio::avb::srp
