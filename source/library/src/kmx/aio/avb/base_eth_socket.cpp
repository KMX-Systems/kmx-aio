/// @file avb/base_eth_socket.cpp
/// @brief Shared non-template helpers for AVB raw Ethernet sockets.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <kmx/aio/avb/base_eth_socket.hpp>

namespace kmx::aio::avb
{
    avb_timestamp_t extract_timestamp_from_ancillary(::msghdr& msg) noexcept
    {
        avb_timestamp_t hw_ts = 0;
        for (::cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg))
        {
            if ((cmsg->cmsg_level == SOL_SOCKET) && (cmsg->cmsg_type == SO_TIMESTAMPING))
            {
                std::array<::timespec, 3u> ts {};
                std::memcpy(ts.data(), CMSG_DATA(cmsg), sizeof(ts));
                hw_ts = timestamp_from_index(ts, 2u);
                if (hw_ts == 0)
                    hw_ts = timestamp_from_index(ts, 0u);
            }
        }

        return hw_ts;
    }
} // namespace kmx::aio::avb