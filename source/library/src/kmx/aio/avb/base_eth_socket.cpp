/// @file avb/base_eth_socket.cpp
/// @brief Shared non-template helpers for AVB raw Ethernet sockets.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <kmx/aio/avb/base_eth_socket.hpp>

namespace kmx::aio::avb
{
    avb_timestamp_t timestamp_from_index(const std::array<::timespec, 3u>& ts, const std::size_t index) noexcept
    {
        if (index >= ts.size() || ts[index].tv_sec <= 0)
            return 0;

        return static_cast<avb_timestamp_t>(ts[index].tv_sec) * 1'000'000'000ULL + static_cast<avb_timestamp_t>(ts[index].tv_nsec);
    }

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

    // primary_eth_socket - setup

    expected_void_t primary_eth_socket::open_socket(const std::string_view iface, const std::uint16_t ethertype)
    {
        ethertype_ = ethertype;

        // AF_PACKET / ETH_P_ALL (or specific EtherType) / SOCK_DGRAM (cooked)
        const int raw_fd = ::socket(AF_PACKET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, ::htons(ethertype == 0 ? ETH_P_ALL : ethertype));
        if (raw_fd < 0)
            return std::unexpected(error_from_errno());

        fd_ = file_descriptor(raw_fd);

        // Query interface index and local MAC
        if (auto res = resolve_iface(iface); !res)
            return std::unexpected(res.error());

        // Bind to interface
        ::sockaddr_ll addr {};
        addr.sll_family = AF_PACKET;
        addr.sll_protocol = ::htons(ethertype == 0 ? ETH_P_ALL : ethertype);
        addr.sll_ifindex = iface_index_;
        if (::bind(raw_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
            return std::unexpected(error_from_errno());

        // Enable SO_TIMESTAMPING: hardware RX + TX timestamps via CLOCK_TAI
        constexpr int ts_flags = SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE |
                                 SOF_TIMESTAMPING_OPT_CMSG | SOF_TIMESTAMPING_OPT_TSONLY;
        if (::setsockopt(raw_fd, SOL_SOCKET, SO_TIMESTAMPING, &ts_flags, sizeof(ts_flags)) < 0)
        {
            // Fall back gracefully — HW timestamping may not be available on all NICs.
            // Software timestamping is used instead.
            constexpr int sw_flags = SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE;
            ::setsockopt(raw_fd, SOL_SOCKET, SO_TIMESTAMPING, &sw_flags, sizeof(sw_flags));
        }

        // Enable SO_TXTIME (for CBS-scheduled transmission)
        ::sock_txtime txtime_cfg {};
        txtime_cfg.clockid = CLOCK_TAI;
        txtime_cfg.flags = 0;

        ::setsockopt(raw_fd, SOL_SOCKET, SO_TXTIME, &txtime_cfg, sizeof(txtime_cfg));

        return {};
    }

    expected_void_t primary_eth_socket::resolve_iface(const std::string_view iface)
    {
        // Get interface index
        ::ifreq ifr {};
        std::strncpy(ifr.ifr_name, iface.data(), IFNAMSIZ - 1);

        if (::ioctl(fd_.get(), SIOCGIFINDEX, &ifr) < 0)
            return std::unexpected(error_from_errno());
        iface_index_ = ifr.ifr_ifindex;

        // Get MAC address
        if (::ioctl(fd_.get(), SIOCGIFHWADDR, &ifr) < 0)
            return std::unexpected(error_from_errno());
        std::memcpy(local_mac_.data(), ifr.ifr_hwaddr.sa_data, ETH_ALEN);

        return {};
    }

    // primary_eth_socket - send (synchronous, non-blocking — called only from completion TUs via base)

    expected_void_t primary_eth_socket::do_send(const mac_address_t& dest_mac, const cspan_byte_t payload,
                                                const std::optional<avb_timestamp_t> tx_time)
    {
        // Build sockaddr_ll destination
        ::sockaddr_ll dest {};
        dest.sll_family = AF_PACKET;
        dest.sll_ifindex = iface_index_;
        dest.sll_protocol = ::htons(ethertype_);
        dest.sll_halen = ETH_ALEN;
        std::memcpy(dest.sll_addr, dest_mac.data(), ETH_ALEN);

        ::msghdr msg {};
        ::iovec iov {const_cast<std::byte*>(payload.data()), payload.size()};
        msg.msg_name = &dest;
        msg.msg_namelen = sizeof(dest);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        // Attach SO_TXTIME control message if scheduled TX was requested
        alignas(::cmsghdr) std::array<std::byte, CMSG_SPACE(sizeof(std::uint64_t))> ctrl_buf {};
        if (tx_time.has_value())
        {
            msg.msg_control = ctrl_buf.data();
            msg.msg_controllen = ctrl_buf.size();
            auto* const cmsg = CMSG_FIRSTHDR(&msg);
            cmsg->cmsg_level = SOL_SOCKET;
            cmsg->cmsg_type = SCM_TXTIME;
            cmsg->cmsg_len = CMSG_LEN(sizeof(std::uint64_t));
            std::memcpy(CMSG_DATA(cmsg), &tx_time.value(), sizeof(std::uint64_t));
        }

        const ssize_t sent = ::sendmsg(fd_.get(), &msg, 0);
        if (sent < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return {}; // Non-blocking; caller should retry after EPOLLOUT

            return std::unexpected(error_from_errno());
        }

        if (static_cast<std::size_t>(sent) != payload.size())
            return std::unexpected(error_from_errno(EIO));

        return {};
    }

    // primary_eth_socket - receive

    std::expected<std::pair<std::vector<std::byte>, avb_timestamp_t>, std::error_code> primary_eth_socket::do_recv()
    {
        // Large enough for max Ethernet frame (1518 bytes)
        std::vector<std::byte> frame_buf(1518);
        alignas(::cmsghdr) std::array<std::byte, 1024u> ctrl_buf {};
        ::sockaddr_ll src {};
        ::iovec iov {frame_buf.data(), frame_buf.size()};
        ::msghdr msg {};
        msg.msg_name = &src;
        msg.msg_namelen = sizeof(src);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = ctrl_buf.data();
        msg.msg_controllen = ctrl_buf.size();

        const ssize_t nr = ::recvmsg(fd_.get(), &msg, 0);
        if (nr < 0)
        {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
                return std::unexpected(error_from_errno(EAGAIN));
            return std::unexpected(error_from_errno());
        }

        frame_buf.resize(static_cast<std::size_t>(nr));

        const avb_timestamp_t hw_ts = extract_timestamp_from_ancillary(msg);

        return std::make_pair(std::move(frame_buf), hw_ts);
    }
} // namespace kmx::aio::avb
