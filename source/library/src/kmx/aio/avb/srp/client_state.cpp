/// @file avb/srp/client_state.cpp
/// @brief Shared non-template part of the IEEE 802.1Qat SRP (MSRP) state machine.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <cstring>
#include <span>

#include <kmx/aio/avb/srp/client_state.hpp>

namespace kmx::aio::avb::srp
{
    namespace
    {
        /// @brief Copies a packed PDU into a byte buffer sized to the PDU.
        /// @param pdu The PDU to serialise.
        /// @return The frame bytes.
        template <typename Pdu>
        [[nodiscard]] std::vector<std::byte> to_bytes(const Pdu& pdu) noexcept
        {
            const auto bytes = std::as_bytes(std::span {&pdu, 1});
            return {bytes.begin(), bytes.end()};
        }
    }

    // Encode helpers

    std::vector<std::byte> primary_client::build_talker_advertise(const stream_descriptor& desc) noexcept
    {
        msrp_talker_pdu_t pdu {};
        const auto attr_val = encode_talker(desc);
        pdu.attr_value = attr_val;

        const std::uint16_t attr_list_len =
            static_cast<std::uint16_t>(sizeof(mrp_vector_header_t) + sizeof(talker_advertise_attr_t) + 1u // three-packed events
                                       + 2u);                                                             // end mark
        pdu.msg_header.attribute_type = static_cast<std::uint8_t>(attr_type::talker_advertise);
        pdu.msg_header.attribute_list_length = ::htons(attr_list_len);
        pdu.vec_header.leave_all_and_num_values = ::htons(1u); // NumValues=1, LeaveAll=0

        return to_bytes(pdu);
    }

    std::vector<std::byte> primary_client::build_listener_ready(const stream_descriptor& desc) noexcept
    {
        msrp_listener_pdu_t pdu {};
        pdu.attr_value.stream_id = encode_stream_id(desc.stream_id);

        const std::uint16_t attr_list_len =
            static_cast<std::uint16_t>(sizeof(mrp_vector_header_t) + sizeof(listener_attr_t) + 1u // three-packed events (declaration)
                                       + 1u                                                       // four-packed events
                                       + 2u);                                                     // end mark
        pdu.msg_header.attribute_type = static_cast<std::uint8_t>(attr_type::listener);
        pdu.msg_header.attribute_list_length = ::htons(attr_list_len);
        pdu.vec_header.leave_all_and_num_values = ::htons(1u);

        // three_packed_events[2:0] = declaration (Ready = 0b010)
        const auto decl = static_cast<std::uint8_t>(listener_decl::ready);
        pdu.three_packed_events = static_cast<std::uint8_t>((decl << 5u) | (decl << 2u) | (decl >> 1u));

        return to_bytes(pdu);
    }

    std::vector<std::byte> primary_client::build_domain() noexcept
    {
        msrp_domain_pdu_t pdu {};
        auto& attr = pdu.attr_value;
        attr.sr_class_id = 6u;       // SR Class A
        attr.sr_class_priority = 3u; // PCP 3
        attr.sr_class_vid = ::htons(2u);

        const std::uint16_t attr_list_len =
            static_cast<std::uint16_t>(sizeof(mrp_vector_header_t) + sizeof(domain_attr_t) + 1u // three-packed events
                                       + 2u);                                                   // end mark
        auto& msg_header = pdu.msg_header;
        msg_header.attribute_type = static_cast<std::uint8_t>(attr_type::domain);
        msg_header.attribute_list_length = ::htons(attr_list_len);
        pdu.vec_header.leave_all_and_num_values = ::htons(1u);

        return to_bytes(pdu);
    }

    // Frame dispatch

    void primary_client::dispatch(const std::byte* const data, const std::size_t len) noexcept
    {
        if (len < 1u + sizeof(mrp_msg_header_t))
            return;

        // Check attribute type
        const auto a_type = static_cast<attr_type>(reinterpret_cast<const mrp_msg_header_t*>(data + 1u)->attribute_type);

        if (a_type == attr_type::talker_advertise)
            on_talker_advertise(data, len);
    }

    // Decode incoming Talker Advertise

    void primary_client::on_talker_advertise(const std::byte* const data, const std::size_t len) noexcept
    {
        // Minimal decode: skip protocol_version(1) + msg_header(3) + vec_header(2)
        constexpr std::size_t offset = 1u + sizeof(mrp_msg_header_t) + sizeof(mrp_vector_header_t);
        if (len < offset + sizeof(talker_advertise_attr_t))
            return;

        const auto* attr = reinterpret_cast<const talker_advertise_attr_t*>(data + offset);

        // Reconstruct stream_descriptor
        stream_descriptor desc {};
        for (std::size_t i = 0; i < 6u; ++i)
            desc.stream_id.source_mac[i] = attr->stream_id[i];
        desc.stream_id.unique_id = (static_cast<std::uint16_t>(attr->stream_id[6]) << 8u) | attr->stream_id[7];
        desc.dest_mac = attr->dest_mac;
        desc.vlan.vid = static_cast<std::uint16_t>(::ntohs(attr->vlan_id));
        desc.max_frame_size = ::ntohs(attr->max_frame_size);
        desc.max_interval_frames = ::ntohs(attr->max_interval_frames);
        desc.priority_and_rank = attr->priority_and_rank;
        desc.accumulated_latency = ::ntohl(attr->accumulated_latency);

        // Notify any pending subscribe() waiters
        if (auto waiter = pending_subs_.find(desc.stream_id); waiter != pending_subs_.end() && !waiter->second.resolved.has_value())
            waiter->second.resolved = desc;
    }
}
