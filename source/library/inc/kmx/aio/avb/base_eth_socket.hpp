/// @file avb/base_eth_socket.hpp
/// @brief Private implementation of raw Ethernet socket for AVB (AF_PACKET + hardware timestamps).
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <array>
#include <cerrno>
#include <cstring>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <linux/errqueue.h>
#include <linux/net_tstamp.h>
#include <linux/sockios.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <kmx/aio/avb/avb_types.hpp>
#include <kmx/aio/basic_types.hpp>
#include <kmx/aio/file_descriptor.hpp>

namespace kmx::aio::avb
{
    /// @brief Private implementation base shared across both AVB execution pillars.
    template <typename Executor>
    struct base_eth_socket
    {
        Executor& exec_;
        file_descriptor fd_ {};
        mac_address_t local_mac_ {};
        int iface_index_ { -1 };
        std::uint16_t ethertype_ {};

        explicit base_eth_socket(Executor& exec) noexcept: exec_(exec) {}

        // ─── Setup ────────────────────────────────────────────────────────────────

        [[nodiscard]] std::expected<void, std::error_code>
        open_socket(std::string_view iface, std::uint16_t ethertype)
        {
            ethertype_ = ethertype;

            // AF_PACKET / ETH_P_ALL (or specific EtherType) / SOCK_DGRAM (cooked)
            const int raw_fd = ::socket(AF_PACKET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                                        ::htons(ethertype == 0 ? ETH_P_ALL : ethertype));
            if (raw_fd < 0)
                return std::unexpected(error_from_errno());

            fd_ = file_descriptor(raw_fd);

            // Query interface index and local MAC
            if (auto res = resolve_iface(iface); !res)
                return std::unexpected(res.error());

            // Bind to interface
            ::sockaddr_ll addr {};
            addr.sll_family   = AF_PACKET;
            addr.sll_protocol = ::htons(ethertype == 0 ? ETH_P_ALL : ethertype);
            addr.sll_ifindex  = iface_index_;
            if (::bind(raw_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
                return std::unexpected(error_from_errno());

            // Enable SO_TIMESTAMPING: hardware RX + TX timestamps via CLOCK_TAI
            const int ts_flags = SOF_TIMESTAMPING_RX_HARDWARE
                               | SOF_TIMESTAMPING_TX_HARDWARE
                               | SOF_TIMESTAMPING_RAW_HARDWARE
                               | SOF_TIMESTAMPING_OPT_CMSG
                               | SOF_TIMESTAMPING_OPT_TSONLY;
            if (::setsockopt(raw_fd, SOL_SOCKET, SO_TIMESTAMPING, &ts_flags, sizeof(ts_flags)) < 0)
            {
                // Fall back gracefully — HW timestamping may not be available on all NICs.
                // Software timestamping is used instead.
                const int sw_flags = SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE;
                ::setsockopt(raw_fd, SOL_SOCKET, SO_TIMESTAMPING, &sw_flags, sizeof(sw_flags));
            }

            // Enable SO_TXTIME (for CBS-scheduled transmission)
            ::sock_txtime txtime_cfg {};
            txtime_cfg.clockid  = CLOCK_TAI;
            txtime_cfg.flags    = 0;
            ::setsockopt(raw_fd, SOL_SOCKET, SO_TXTIME, &txtime_cfg, sizeof(txtime_cfg));

            return {};
        }

        // ─── Send ─────────────────────────────────────────────────────────────────

        [[nodiscard]] std::expected<void, std::error_code>
        do_send(const mac_address_t& dest_mac, std::span<const std::byte> payload,
                std::optional<avb_timestamp_t> tx_time)
        {
            // Build sockaddr_ll destination
            ::sockaddr_ll dest {};
            dest.sll_family   = AF_PACKET;
            dest.sll_ifindex  = iface_index_;
            dest.sll_protocol = ::htons(ethertype_);
            dest.sll_halen    = ETH_ALEN;
            std::memcpy(dest.sll_addr, dest_mac.data(), ETH_ALEN);

            ::msghdr msg {};
            ::iovec  iov { const_cast<std::byte*>(payload.data()), payload.size() };
            msg.msg_name    = &dest;
            msg.msg_namelen = sizeof(dest);
            msg.msg_iov     = &iov;
            msg.msg_iovlen  = 1;

            // Attach SO_TXTIME control message if scheduled TX was requested
            alignas(::cmsghdr) std::array<std::byte, CMSG_SPACE(sizeof(std::uint64_t))> ctrl_buf {};
            if (tx_time.has_value())
            {
                msg.msg_control    = ctrl_buf.data();
                msg.msg_controllen = ctrl_buf.size();
                auto* cmsg         = CMSG_FIRSTHDR(&msg);
                cmsg->cmsg_level   = SOL_SOCKET;
                cmsg->cmsg_type    = SCM_TXTIME;
                cmsg->cmsg_len     = CMSG_LEN(sizeof(std::uint64_t));
                std::memcpy(CMSG_DATA(cmsg), &tx_time.value(), sizeof(std::uint64_t));
            }

            const ssize_t sent = ::sendmsg(fd_.get(), &msg, 0);
            if (sent < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    return {};  // Non-blocking; caller should retry after EPOLLOUT
                return std::unexpected(error_from_errno());
            }
            return {};
        }

        // ─── Receive ──────────────────────────────────────────────────────────────

        [[nodiscard]] std::expected<std::pair<std::vector<std::byte>, avb_timestamp_t>,
                                    std::error_code>
        do_recv()
        {
            // Large enough for max Ethernet frame (1518 bytes)
            std::vector<std::byte> frame_buf(1518);

            alignas(::cmsghdr) std::array<std::byte, 1024> ctrl_buf {};
            ::sockaddr_ll src {};
            ::iovec iov { frame_buf.data(), frame_buf.size() };
            ::msghdr msg {};
            msg.msg_name       = &src;
            msg.msg_namelen    = sizeof(src);
            msg.msg_iov        = &iov;
            msg.msg_iovlen     = 1;
            msg.msg_control    = ctrl_buf.data();
            msg.msg_controllen = ctrl_buf.size();

            const ssize_t nr = ::recvmsg(fd_.get(), &msg, 0);
            if (nr < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    return std::unexpected(error_from_errno(EAGAIN));
                return std::unexpected(error_from_errno());
            }

            frame_buf.resize(static_cast<std::size_t>(nr));

            // Extract hardware/software timestamp from ancillary data
            avb_timestamp_t hw_ts = 0;
            for (const ::cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
                 cmsg = CMSG_NXTHDR(&msg, cmsg))
            {
                if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_TIMESTAMPING)
                {
                    // kernel returns three timespec64 values: software, _, hardware
                    std::array<::timespec, 3> ts {};
                    std::memcpy(ts.data(), CMSG_DATA(cmsg), sizeof(ts));
                    // Prefer hardware [2] then software [0]
                    if (ts[2].tv_sec > 0)
                        hw_ts = static_cast<avb_timestamp_t>(ts[2].tv_sec) * 1'000'000'000ULL
                              + static_cast<avb_timestamp_t>(ts[2].tv_nsec);
                    else if (ts[0].tv_sec > 0)
                        hw_ts = static_cast<avb_timestamp_t>(ts[0].tv_sec) * 1'000'000'000ULL
                              + static_cast<avb_timestamp_t>(ts[0].tv_nsec);
                }
            }

            return std::make_pair(std::move(frame_buf), hw_ts);
        }

    private:
        [[nodiscard]] std::expected<void, std::error_code>
        resolve_iface(std::string_view iface)
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
    };
}
