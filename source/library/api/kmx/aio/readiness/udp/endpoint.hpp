/// @file aio/readiness/udp/endpoint.hpp
/// @brief Readiness-model UDP endpoint using epoll-based async I/O.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <expected>
    #include <span>
    #include <sys/socket.h>

    #include <kmx/aio/readiness/udp/socket.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::readiness::udp
{
    /// @brief High-level UDP endpoint providing span/address-based send/recv over readiness::udp::socket.
    class endpoint
    {
    public:
        using result_task = socket::result_task;
        /// @brief Result type returned by endpoint::create.
        /// @details Contains a fully initialized endpoint on success, or an error code on failure.
        using create_result = std::expected<endpoint, std::error_code>;

        /// @brief Create a UDP endpoint bound to an executor.
        /// @param exec Executor that drives asynchronous readiness operations.
        /// @param domain Socket address family (for example AF_INET or AF_INET6).
        /// @return A ready-to-use endpoint on success, otherwise a system error code.
        [[nodiscard]] static create_result create(executor& exec, const int domain = AF_INET) noexcept;

        /// @brief Construct an endpoint from an existing readiness UDP socket.
        /// @param sock Socket instance to own.
        explicit endpoint(socket&& sock) noexcept: socket_(std::move(sock)) {}

        endpoint(endpoint&&) noexcept = default;
        endpoint& operator=(endpoint&&) noexcept = delete;

        /// @brief Access the underlying socket.
        /// @return Mutable reference to the owned socket.
        [[nodiscard]] socket& raw() noexcept { return socket_; }
        /// @brief Access the underlying socket.
        /// @return Const reference to the owned socket.
        [[nodiscard]] const socket& raw() const noexcept { return socket_; }

        /// @brief Receive a datagram and peer endpoint metadata.
        /// @param buffer Destination byte span for payload data.
        /// @param peer_addr Output storage for the sender socket address.
        /// @param out_peer_addr_len In/out address storage length populated by recv.
        /// @return Awaitable task producing bytes received or an error.
        /// @throws std::system_error If submission or await path fails.
        [[nodiscard]] result_task recv(std::span<std::byte> buffer, sockaddr_storage& peer_addr,
                                       ::socklen_t& out_peer_addr_len) noexcept(false);

        /// @brief Receive a datagram and decode sender IP/port in addition to raw sockaddr data.
        /// @param buffer Destination byte span for payload data.
        /// @param peer_addr Output storage for the sender socket address.
        /// @param out_peer_addr_len In/out address storage length populated by recv.
        /// @param out_peer_ip Parsed sender IP address.
        /// @param out_peer_port Parsed sender UDP port.
        /// @return Awaitable task producing bytes received or an error.
        /// @throws std::system_error If submission or await path fails.
        [[nodiscard]] result_task recv(std::span<std::byte> buffer, sockaddr_storage& peer_addr, ::socklen_t& out_peer_addr_len,
                                       ip_address_t& out_peer_ip, port_t& out_peer_port) noexcept(false);

        /// @brief Send a datagram to a peer address represented as sockaddr.
        /// @param buffer Source byte span to transmit.
        /// @param peer_addr Destination socket address.
        /// @param addr_len Length of @p peer_addr.
        /// @return Awaitable task producing bytes sent or an error.
        /// @throws std::system_error If submission or await path fails.
        [[nodiscard]] result_task send(std::span<const std::byte> buffer, const sockaddr* peer_addr, ::socklen_t addr_len) noexcept(false);

        /// @brief Send a datagram to a peer address represented as IP and port.
        /// @param buffer Source byte span to transmit.
        /// @param peer_ip Destination IP address.
        /// @param peer_port Destination UDP port.
        /// @return Awaitable task producing bytes sent or an error.
        /// @throws std::system_error If submission or await path fails.
        [[nodiscard]] result_task send(std::span<const std::byte> buffer, ip_address_t peer_ip, port_t peer_port) noexcept(false);

    private:
        socket socket_;
    };
} // namespace kmx::aio::readiness::udp
