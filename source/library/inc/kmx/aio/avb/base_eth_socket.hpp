/// @file avb/base_eth_socket.hpp
/// @brief Private implementation of raw Ethernet socket for AVB (AF_PACKET + hardware timestamps).
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cerrno>
    #include <cstring>
    #include <expected>
    #include <optional>
    #include <span>
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
    #include <time.h>

    #include <kmx/aio/avb/avb_types.hpp>
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/file_descriptor.hpp>
#endif

namespace kmx::aio::avb
{
    /// @brief Converts one entry of a `SCM_TIMESTAMPING` triple into nanoseconds.
    /// @param ts    The three timestamps the kernel reported (software, legacy, hardware).
    /// @param index Which of the three to read.
    /// @return The timestamp in nanoseconds, or 0 when the entry is absent or unset.
    [[nodiscard]] inline avb_timestamp_t timestamp_from_index(const std::array<::timespec, 3u>& ts, const std::size_t index) noexcept
    {
        if (index >= ts.size() || ts[index].tv_sec <= 0)
            return 0;

        return static_cast<avb_timestamp_t>(ts[index].tv_sec) * 1'000'000'000ULL + static_cast<avb_timestamp_t>(ts[index].tv_nsec);
    }

    /// @brief Extracts the hardware receive timestamp from a message's ancillary data.
    /// @param msg The received message whose control buffer is scanned for `SCM_TIMESTAMPING`.
    /// @return The hardware timestamp in nanoseconds, or 0 when the NIC supplied none.
    [[nodiscard]] avb_timestamp_t extract_timestamp_from_ancillary(::msghdr& msg) noexcept;

    /// @brief Private implementation base shared across both AVB execution pillars.
    template <typename Executor>
    struct base_eth_socket
    {
        /// @brief The executor the raw socket is registered with.
        Executor& exec_;
        /// @brief The `AF_PACKET` socket bound to the interface.
        file_descriptor fd_ {};
        /// @brief MAC address of the bound interface.
        mac_address_t local_mac_ {};
        /// @brief Interface index of the bound interface, or -1 while unbound.
        int iface_index_ {-1};
        /// @brief The EtherType receives are filtered on.
        std::uint16_t ethertype_ {};

        /// @brief Constructs an unopened socket bound to an executor.
        /// @param exec The executor that will drive the socket's I/O.
        explicit base_eth_socket(Executor& exec) noexcept: exec_(exec) {}

        /// @brief Unregisters the descriptor from the executor, if it registers descriptors at all.
        ~base_eth_socket() noexcept
        {
            if constexpr (requires(Executor& e, fd_t fd) { e.unregister_fd(fd); })
                if (fd_.is_valid())
                    exec_.unregister_fd(fd_.get());
        }

        // Setup

        /// @brief Opens an `AF_PACKET` socket, binds it to an interface, and enables hardware timestamping.
        /// @param iface     Network interface name (e.g. "eth0").
        /// @param ethertype EtherType to filter on receive; 0 or `ETH_P_ALL` receives everything.
        /// @return Success, or the error code the failing syscall reported.
        [[nodiscard]] expected_void_t open_socket(const std::string_view iface, std::uint16_t ethertype)
        {
            ethertype_ = ethertype;

            // AF_PACKET / ETH_P_ALL (or specific EtherType) / SOCK_DGRAM (cooked)
            const int raw_fd =
                ::socket(AF_PACKET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, ::htons(ethertype == 0 ? ETH_P_ALL : ethertype));
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

            // Register with the epoll-based executor so async_recvmsg/async_sendmsg
            // can suspend on EPOLLIN/EPOLLOUT events. Completion-model executors use
            // io_uring ops directly and do not expose register_fd.
            if constexpr (requires(Executor& e, fd_t fd) { e.register_fd(fd); })
            {
                if (auto reg = exec_.register_fd(raw_fd); !reg)
                    return std::unexpected(reg.error());
            }

            return {};
        }

        // Send (synchronous, non-blocking — called only from completion TUs via base)

        /// @brief Sends one Layer 2 frame, optionally scheduled with `SO_TXTIME`.
        /// @param dest_mac Destination MAC address.
        /// @param payload  Payload bytes; the kernel prepends the L2 header.
        /// @param tx_time  TAI transmission time for scheduled TX; sends immediately when empty.
        /// @return Success, or the error code `sendmsg` reported.
        [[nodiscard]] expected_void_t do_send(const mac_address_t& dest_mac, std::span<const std::byte> payload,
                                              std::optional<avb_timestamp_t> tx_time)
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

        // Receive

        /// @brief Receives one frame together with its hardware receive timestamp.
        /// @return The frame bytes paired with the TAI receive timestamp, or an error code.
        [[nodiscard]] std::expected<std::pair<std::vector<std::byte>, avb_timestamp_t>, std::error_code> do_recv()
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

    private:
        /// @brief Resolves an interface name to its index and MAC address.
        /// @param iface Network interface name.
        /// @return Success, or the error code the failing `ioctl` reported.
        [[nodiscard]] expected_void_t resolve_iface(const std::string_view iface)
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
