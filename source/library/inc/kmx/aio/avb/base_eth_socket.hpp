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
    [[nodiscard]] avb_timestamp_t timestamp_from_index(const std::array<::timespec, 3u>& ts, std::size_t index) noexcept;

    /// @brief Extracts the hardware receive timestamp from a message's ancillary data.
    /// @param msg The received message whose control buffer is scanned for `SCM_TIMESTAMPING`.
    /// @return The hardware timestamp in nanoseconds, or 0 when the NIC supplied none.
    [[nodiscard]] avb_timestamp_t extract_timestamp_from_ancillary(::msghdr& msg) noexcept;

    /// @brief The control buffer an SO_TXTIME message needs.
    using tx_time_control_t = std::array<std::byte, CMSG_SPACE(sizeof(std::uint64_t))>;

    /// @brief Attaches the SO_TXTIME control message that paces a frame's departure.
    /// @param msg The message to attach it to.
    /// @param control Storage for the control message; must outlive the send that reads it.
    /// @param tx_time When the frame is to leave the interface.
    /// @details SO_TXTIME hands the frame to the qdisc with a launch time, which is what lets a
    ///          credit-based shaper pace it instead of sending it as soon as the queue drains.
    void attach_tx_time(::msghdr& msg, tx_time_control_t& control, avb_timestamp_t tx_time) noexcept;

    /// @brief Builds the message one AVB frame is sent as.
    /// @param msg The message header to fill in.
    /// @param dest Storage for the link-layer destination.
    /// @param iov Storage for the single payload segment.
    /// @param control Storage for the SO_TXTIME control message.
    /// @param iface_index The interface to send on.
    /// @param ethertype The EtherType to send under.
    /// @param dest_mac The destination MAC address.
    /// @param payload The frame's payload.
    /// @param tx_time When the frame is to leave the interface, when its departure is scheduled.
    /// @note Every one of @p dest, @p iov and @p control is pointed at by @p msg, so all three have to
    ///       outlive the `sendmsg()` call that reads them.
    void prepare_frame_message(::msghdr& msg, ::sockaddr_ll& dest, ::iovec& iov, tx_time_control_t& control, int iface_index,
                               std::uint16_t ethertype, const mac_address_t& dest_mac, cspan_byte_t payload,
                               const std::optional<avb_timestamp_t>& tx_time) noexcept;

    /// @brief Executor-agnostic part of the AVB raw socket: descriptor, interface state and syscalls.
    /// @details Holds everything that does not depend on the executor type, so the code is emitted once
    ///          instead of once per pillar. `base_eth_socket` adds the executor-specific registration.
    struct primary_eth_socket
    {
        /// @brief The `AF_PACKET` socket bound to the interface.
        file_descriptor fd_ {};
        /// @brief MAC address of the bound interface.
        mac_address_t local_mac_ {};
        /// @brief Interface index of the bound interface, or -1 while unbound.
        int iface_index_ {-1};
        /// @brief The EtherType receives are filtered on.
        std::uint16_t ethertype_ {};

        // Setup

        /// @brief Opens an `AF_PACKET` socket, binds it to an interface, and enables hardware timestamping.
        /// @param iface     Network interface name (e.g. "eth0").
        /// @param ethertype EtherType to filter on receive; 0 or `ETH_P_ALL` receives everything.
        /// @return Success, or the error code the failing syscall reported.
        [[nodiscard]] expected_void_t open_socket(std::string_view iface, std::uint16_t ethertype);

        // Send (synchronous, non-blocking — called only from completion TUs via base)

        /// @brief Sends one Layer 2 frame, optionally scheduled with `SO_TXTIME`.
        /// @param dest_mac Destination MAC address.
        /// @param payload  Payload bytes; the kernel prepends the L2 header.
        /// @param tx_time  TAI transmission time for scheduled TX; sends immediately when empty.
        /// @return Success, or the error code `sendmsg` reported.
        [[nodiscard]] expected_void_t do_send(const mac_address_t& dest_mac, cspan_byte_t payload, std::optional<avb_timestamp_t> tx_time);

        // Receive

        /// @brief Receives one frame together with its hardware receive timestamp.
        /// @return The frame bytes paired with the TAI receive timestamp, or an error code.
        [[nodiscard]] std::expected<std::pair<std::vector<std::byte>, avb_timestamp_t>, std::error_code> do_recv();

    private:
        /// @brief Resolves an interface name to its index and MAC address.
        /// @param iface Network interface name.
        /// @return Success, or the error code the failing `ioctl` reported.
        [[nodiscard]] expected_void_t resolve_iface(std::string_view iface);
    };

    /// @brief Private implementation base shared across both AVB execution pillars.
    template <typename Executor>
    struct base_eth_socket: primary_eth_socket
    {
        /// @brief The executor the raw socket is registered with.
        Executor& exec_;

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

        /// @brief Opens the socket and registers it with the executor when the executor requires that.
        /// @param iface     Network interface name (e.g. "eth0").
        /// @param ethertype EtherType to filter on receive; 0 or `ETH_P_ALL` receives everything.
        /// @return Success, or the error code the failing syscall reported.
        [[nodiscard]] expected_void_t open_socket(const std::string_view iface, std::uint16_t ethertype)
        {
            if (auto res = primary_eth_socket::open_socket(iface, ethertype); !res)
                return std::unexpected(res.error());

            // Register with the epoll-based executor so async_recvmsg/async_sendmsg
            // can suspend on EPOLLIN/EPOLLOUT events. Completion-model executors use
            // io_uring ops directly and do not expose register_fd.
            if constexpr (requires(Executor& e, fd_t fd) { e.register_fd(fd); })
                if (auto reg = exec_.register_fd(fd_.get()); !reg)
                    return std::unexpected(reg.error());

            return {};
        }
    };
}
