/// @file aio/readiness/udp/endpoint.hpp
/// @brief Readiness-model UDP endpoint using epoll-based async I/O.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <expected>
    #include <span>
    #include <sys/socket.h>

    #include <kmx/aio/task.hpp>
    #include <kmx/aio/readiness/udp/socket.hpp>
#endif

namespace kmx::aio::readiness::udp
{
    /// @brief High-level UDP endpoint providing span/address-based send/recv over readiness::udp::socket.
    class endpoint
    {
    public:
        using result_task = socket::result_task;
        using create_result = std::expected<endpoint, std::error_code>;

        [[nodiscard]] static create_result create(executor& exec, int domain = AF_INET) noexcept;

        explicit endpoint(socket&& sock) noexcept: socket_(std::move(sock)) {}

        endpoint(endpoint&&) noexcept = default;
        endpoint& operator=(endpoint&&) noexcept = delete;

        [[nodiscard]] socket& raw() noexcept { return socket_; }
        [[nodiscard]] const socket& raw() const noexcept { return socket_; }

        [[nodiscard]] result_task recv(std::span<std::byte> buffer, sockaddr_storage& peer_addr,
                                       ::socklen_t& out_peer_addr_len) noexcept(false);

        [[nodiscard]] result_task recv(std::span<std::byte> buffer, sockaddr_storage& peer_addr,
                                       ::socklen_t& out_peer_addr_len, ip_address_t& out_peer_ip,
                                       port_t& out_peer_port) noexcept(false);

        [[nodiscard]] result_task send(std::span<const std::byte> buffer, const sockaddr* peer_addr,
                                       ::socklen_t addr_len) noexcept(false);

        [[nodiscard]] result_task send(std::span<const std::byte> buffer, ip_address_t peer_ip,
                                       port_t peer_port) noexcept(false);

    private:
        socket socket_;
    };
} // namespace kmx::aio::readiness::udp
