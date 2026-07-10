/// @file avb/srp/messages.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <kmx/aio/avb/srp/messages.hpp>

#include <arpa/inet.h>

namespace kmx::aio::avb::srp
{
    std::array<std::uint8_t, 8u> encode_stream_id(const stream_id_t& sid) noexcept
    {
        std::array<std::uint8_t, 8u> out {};
        for (std::size_t i = 0; i < 6u; ++i)
            out[i] = sid.source_mac[i];
        out[6] = static_cast<std::uint8_t>(sid.unique_id >> 8u);
        out[7] = static_cast<std::uint8_t>(sid.unique_id & 0xFFu);
        return out;
    }

    talker_advertise_attr_t encode_talker(const stream_descriptor& desc) noexcept
    {
        talker_advertise_attr_t a {};
        a.stream_id = encode_stream_id(desc.stream_id);
        a.dest_mac = desc.dest_mac;
        a.vlan_id = ::htons(desc.vlan.vid);
        a.max_frame_size = ::htons(desc.max_frame_size);
        a.max_interval_frames = ::htons(desc.max_interval_frames);
        a.priority_and_rank = desc.priority_and_rank;
        a.accumulated_latency = ::htonl(desc.accumulated_latency);
        return a;
    }

} // namespace kmx::aio::avb::srp
