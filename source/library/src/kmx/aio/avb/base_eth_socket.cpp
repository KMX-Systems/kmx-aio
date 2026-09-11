/// @file src/kmx/aio/avb/base_eth_socket.cpp
/// @brief Shared non-template helpers for AVB raw Ethernet sockets.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/avb/base_eth_socket.hpp>
#ifndef PCH
    #include <cstring>
    #include <arpa/inet.h>
    #include <net/ethernet.h>
#endif

namespace kmx::aio::avb
{
    tai_timestamp_t timestamp_from_index(const std::array<::timespec, 3u>& ts, const std::size_t index) noexcept
    {
        if ((index >= ts.size()) || (ts[index].tv_sec <= 0))
            return 0;

        return static_cast<tai_timestamp_t>(ts[index].tv_sec) * 1'000'000'000ULL + static_cast<tai_timestamp_t>(ts[index].tv_nsec);
    }

    void attach_tx_time(::msghdr& msg, tx_time_control_t& control, const tai_timestamp_t tx_time) noexcept
    {
        msg.msg_control = control.data();
        msg.msg_controllen = control.size();
        auto* const cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_TXTIME;
        cmsg->cmsg_len = CMSG_LEN(sizeof(std::uint64_t));
        std::memcpy(CMSG_DATA(cmsg), &tx_time, sizeof(std::uint64_t));
    }

    void prepare_frame_message(frame_message& message, const outgoing_frame& frame) noexcept
    {
        auto& destination = message.destination;
        destination = {};
        destination.sll_family = AF_PACKET;
        destination.sll_ifindex = frame.iface_index;
        destination.sll_protocol = ::htons(frame.ethertype);
        destination.sll_halen = ETH_ALEN;
        std::memcpy(destination.sll_addr, frame.dest_mac.data(), ETH_ALEN);

        message.segment = {const_cast<std::byte*>(frame.payload.data()), frame.payload.size()};

        auto& header = message.header;
        header = {};
        header.msg_name = &destination;
        header.msg_namelen = sizeof(destination);
        header.msg_iov = &message.segment;
        header.msg_iovlen = 1;

        if (frame.tx_time.has_value())
            attach_tx_time(header, message.control, *frame.tx_time);
    }

    tai_timestamp_t extract_timestamp_from_ancillary(::msghdr& msg) noexcept
    {
        tai_timestamp_t hw_ts {};
        for (::cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg))
            if ((cmsg->cmsg_level == SOL_SOCKET) && (cmsg->cmsg_type == SO_TIMESTAMPING))
            {
                std::array<::timespec, 3u> ts {};
                std::memcpy(ts.data(), CMSG_DATA(cmsg), sizeof(ts));
                hw_ts = timestamp_from_index(ts, 2u);
                if (hw_ts == 0)
                    hw_ts = timestamp_from_index(ts, 0u);
            }

        return hw_ts;
    }
}
