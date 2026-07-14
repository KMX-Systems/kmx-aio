/// @file aio/completion/udp/endpoint.hpp
/// @brief Completion-model UDP endpoint using io_uring-based async I/O.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <expected>
    #include <span>
    #include <sys/socket.h>

    #include <kmx/aio/completion/udp/socket.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::completion::udp
{
    /// @brief High-level UDP endpoint providing span/address-based send/recv over completion::udp::socket.
    class endpoint
    {
    public:
        /// @brief Task type returned by asynchronous endpoint operations.
        using result_task = socket::result_task;
        /// @brief Result type returned by endpoint creation.
        using create_result = std::expected<endpoint, std::error_code>;

        /// @brief Creates a UDP endpoint for the requested address family.
        /// @param exec Completion executor used for I/O scheduling.
        /// @param domain Socket domain, such as AF_INET or AF_INET6.
        /// @return A constructed endpoint or an error code.
        [[nodiscard]] static create_result create(executor& exec, const int domain = AF_INET) noexcept;

        /// @brief Wraps an already-opened socket.
        explicit endpoint(socket&& sock) noexcept: socket_(std::move(sock)) {}

        /// @brief Moves the endpoint.
        endpoint(endpoint&&) noexcept = default;
        /// @brief Non-copyable assignment.
        endpoint& operator=(endpoint&&) noexcept = delete;

        /// @brief Returns the underlying socket.
        /// @return The owned socket instance.
        [[nodiscard]] socket& raw() noexcept { return socket_; }
        /// @brief Returns the underlying socket.
        /// @return The owned socket instance.
        [[nodiscard]] const socket& raw() const noexcept { return socket_; }

        /// @brief Receives a datagram into a buffer and returns the peer address.
        /// @param buffer Destination buffer for payload bytes.
        /// @param peer_addr Output socket address for the sender.
        /// @param out_peer_addr_len Output length of the sender address.
        /// @return A task yielding the received byte count or an error.
        [[nodiscard]] result_task recv(std::span<std::byte> buffer, sockaddr_storage& peer_addr,
                                       ::socklen_t& out_peer_addr_len) noexcept(false);

        /// @brief Receives a datagram and decodes the peer IP and port.
        /// @param buffer Destination buffer for payload bytes.
        /// @param peer_addr Output socket address for the sender.
        /// @param out_peer_addr_len Output length of the sender address.
        /// @param out_peer_ip Output peer IP address.
        /// @param out_peer_port Output peer port.
        /// @return A task yielding the received byte count or an error.
        [[nodiscard]] result_task recv(std::span<std::byte> buffer, sockaddr_storage& peer_addr, ::socklen_t& out_peer_addr_len,
                                       ip_address_t& out_peer_ip, port_t& out_peer_port) noexcept(false);

        /// @brief Sends a datagram to a raw socket address.
        /// @param buffer Payload bytes to send.
        /// @param peer_addr Destination address.
        /// @param addr_len Length of the destination address.
        /// @return A task yielding the sent byte count or an error.
        [[nodiscard]] result_task send(std::span<const std::byte> buffer, const sockaddr* peer_addr, ::socklen_t addr_len) noexcept(false);

        /// @brief Sends a datagram to an IP/port destination.
        /// @param buffer Payload bytes to send.
        /// @param peer_ip Destination IP address.
        /// @param peer_port Destination port.
        /// @return A task yielding the sent byte count or an error.
        [[nodiscard]] result_task send(std::span<const std::byte> buffer, ip_address_t peer_ip, port_t peer_port) noexcept(false);

    private:
        /// @brief Owned completion-model socket backing the endpoint.
        socket socket_;
    };
} // namespace kmx::aio::completion::udp
