/// @file avb/eth_socket.hpp
/// @brief Public API for a raw Ethernet socket with hardware timestamping (AVB/TSN).
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <kmx/aio/avb/avb_types.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::avb
{
    /// @brief Async raw Ethernet socket for AVB/TSN frame I/O.
    ///
    /// Uses AF_PACKET + SOCK_DGRAM (cooked) for zero-copy Layer 2 send/receive.
    /// Hardware RX timestamps are retrieved via SO_TIMESTAMPING (CLOCK_TAI).
    /// Scheduled TX uses SO_TXTIME for deterministic CBS-shaped transmission.
    ///
    /// @note Requires CAP_NET_RAW capability and Linux kernel ≥ 5.13.
    ///
    /// @example
    /// @code
    ///   avb::eth_socket sock(executor);
    ///   co_await sock.open("eth0", avb::ethertype::avtp);
    ///   co_await sock.send(frame_bytes);
    ///   auto [data, ts] = *(co_await sock.recv());
    /// @endcode
    template <typename Executor>
    class generic_eth_socket
    {
    public:
        explicit generic_eth_socket(Executor& exec) noexcept;
        ~generic_eth_socket();

        generic_eth_socket(const generic_eth_socket&)            = delete;
        generic_eth_socket& operator=(const generic_eth_socket&) = delete;
        generic_eth_socket(generic_eth_socket&&)                 = default;
        generic_eth_socket& operator=(generic_eth_socket&&)      = default;

        /// @brief Open the socket, binding to a specific NIC and EtherType filter.
        /// @param iface     Network interface name (e.g. "eth0").
        /// @param ethertype EtherType to filter on receive (e.g. avb::ethertype::avtp).
        ///                  Use 0 or ETH_P_ALL to receive all frames.
        /// @return Success or an error code.
        [[nodiscard]] task<std::expected<void, std::error_code>>
        open(std::string_view iface, std::uint16_t ethertype) noexcept(false);

        /// @brief Send a raw Layer 2 frame.
        /// @param dest_mac  Destination MAC address.
        /// @param frame     Payload bytes (not including L2 header — kernel adds it).
        /// @param tx_time   Optional TAI transmission timestamp for scheduled TX (SO_TXTIME).
        ///                  If empty, frame is sent immediately.
        /// @return Success or an error code.
        [[nodiscard]] task<std::expected<void, std::error_code>>
        send(const mac_address_t& dest_mac, std::span<const std::byte> frame,
             std::optional<avb_timestamp_t> tx_time = {}) noexcept(false);

        /// @brief Receive the next frame matching the bound EtherType.
        /// @return Pair of {frame bytes, hardware RX TAI timestamp} or an error.
        [[nodiscard]] task<std::expected<std::pair<std::vector<std::byte>, avb_timestamp_t>,
                                         std::error_code>>
        recv() noexcept(false);

        /// @brief Return the MAC address of the bound interface.
        [[nodiscard]] mac_address_t local_mac() const noexcept;

        /// @brief Return the interface index of the bound interface.
        [[nodiscard]] int iface_index() const noexcept;

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };
}

// Pillar-specific aliases are defined in their respective headers:
//   kmx/aio/completion/avb/eth_socket.hpp
//   kmx/aio/readiness/avb/eth_socket.hpp
