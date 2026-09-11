/// @file inc/kmx/aio/avb/base_eth_socket.hpp
/// @brief Private implementation of raw Ethernet socket for AVB (AF_PACKET + hardware timestamps).
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/avb/avb_types.hpp>
    #include <kmx/aio/avb/primary_eth_socket.hpp>
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/file_descriptor.hpp>

    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <expected>
    #include <optional>
    #include <string_view>
    #include <netpacket/packet.h>
    #include <sys/socket.h>
    #include <time.h>
#endif

namespace kmx::aio::avb
{
    /// @brief Converts one entry of a `SCM_TIMESTAMPING` triple into nanoseconds.
    /// @param ts    The three timestamps the kernel reported (software, legacy, hardware).
    /// @param index Which of the three to read.
    /// @return The timestamp in nanoseconds, or 0 when the entry is absent or unset.
    [[nodiscard]] tai_timestamp_t timestamp_from_index(const std::array<::timespec, 3u>& ts, std::size_t index) noexcept;

    /// @brief Extracts the hardware receive timestamp from a message's ancillary data.
    /// @param msg The received message whose control buffer is scanned for `SCM_TIMESTAMPING`.
    /// @return The hardware timestamp in nanoseconds, or 0 when the NIC supplied none.
    [[nodiscard]] tai_timestamp_t extract_timestamp_from_ancillary(::msghdr& msg) noexcept;

    /// @brief The control buffer an SO_TXTIME message needs.
    using tx_time_control_t = std::array<std::byte, CMSG_SPACE(sizeof(std::uint64_t))>;

    /// @brief Attaches the SO_TXTIME control message that paces a frame's departure.
    /// @param msg The message to attach it to.
    /// @param control Storage for the control message; must outlive the send that reads it.
    /// @param tx_time When the frame is to leave the interface.
    /// @details SO_TXTIME hands the frame to the qdisc with a launch time, which is what lets a
    ///          credit-based shaper pace it instead of sending it as soon as the queue drains.
    void attach_tx_time(::msghdr& msg, tx_time_control_t& control, tai_timestamp_t tx_time) noexcept;

    /// @brief One AVB frame as `sendmsg()` is handed it: the message header and the storage the header points into.
    /// @note Once prepared, @ref header points at the other members, so the object must be neither copied nor moved
    ///       afterwards, and has to outlive the `sendmsg()` call that reads it.
    struct frame_message
    {
        /// @brief The message header passed to `sendmsg()`.
        ::msghdr header {};
        /// @brief The link-layer destination the header names.
        ::sockaddr_ll destination {};
        /// @brief The single payload segment the header names.
        ::iovec segment {};
        /// @brief The SO_TXTIME control message the header names when the departure is scheduled.
        alignas(::cmsghdr) tx_time_control_t control {};
    };

    /// @brief One outgoing AVB frame: where it is sent, what it carries and when it leaves.
    struct outgoing_frame
    {
        /// @brief The interface to send on.
        int iface_index {-1};
        /// @brief The EtherType to send under.
        std::uint16_t ethertype {};
        /// @brief The destination MAC address.
        mac_address_t dest_mac {};
        /// @brief The frame's payload; the kernel prepends the L2 header.
        cspan_byte_t payload {};
        /// @brief When the frame is to leave the interface; it is sent immediately when empty.
        std::optional<tai_timestamp_t> tx_time {};
    };

    /// @brief Builds the message one AVB frame is sent as.
    /// @param message The message to fill in; its header ends up pointing at its other members.
    /// @param frame The frame to send.
    void prepare_frame_message(frame_message& message, const outgoing_frame& frame) noexcept;

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
