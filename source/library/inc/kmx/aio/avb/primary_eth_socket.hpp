/// @file inc/kmx/aio/avb/primary_eth_socket.hpp
/// @brief Executor-agnostic part of the AVB raw Ethernet socket (AF_PACKET + hardware timestamps).
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/avb/avb_types.hpp>
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/file_descriptor.hpp>

    #include <cstddef>
    #include <cstdint>
    #include <expected>
    #include <optional>
    #include <string_view>
    #include <system_error>
    #include <utility>
    #include <vector>
#endif

namespace kmx::aio::avb
{
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
        [[nodiscard]] expected_void_t do_send(const mac_address_t& dest_mac, cspan_byte_t payload, std::optional<tai_timestamp_t> tx_time);

        // Receive

        /// @brief Receives one frame together with its hardware receive timestamp.
        /// @return The frame bytes paired with the TAI receive timestamp, or an error code.
        [[nodiscard]] std::expected<std::pair<std::vector<std::byte>, tai_timestamp_t>, std::error_code> do_recv();

    private:
        /// @brief Resolves an interface name to its index and MAC address.
        /// @param iface Network interface name.
        /// @return Success, or the error code the failing `ioctl` reported.
        [[nodiscard]] expected_void_t resolve_iface(std::string_view iface);
    };
}
