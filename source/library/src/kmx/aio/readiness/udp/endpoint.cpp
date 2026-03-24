/// @file aio/readiness/udp/endpoint.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/readiness/udp/endpoint.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/uio.h>

namespace kmx::aio::readiness::udp
{
    endpoint::create_result endpoint::create(executor& exec, const int domain) noexcept
    {
        auto sock = socket::create(exec, domain);
        if (!sock)
            return std::unexpected(sock.error());

        return endpoint(std::move(*sock));
    }

    endpoint::result_task endpoint::recv(std::span<std::byte> buffer, sockaddr_storage& peer_addr,
                                         ::socklen_t& out_peer_addr_len) noexcept(false)
    {
        iovec iov {};
        iov.iov_base = buffer.data();
        iov.iov_len = buffer.size();

        msghdr msg {};
        msg.msg_name = &peer_addr;
        msg.msg_namelen = sizeof(peer_addr);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        auto result = co_await socket_.recvmsg(&msg);
        if (result)
            out_peer_addr_len = msg.msg_namelen;

        co_return result;
    }

    endpoint::result_task endpoint::recv(std::span<std::byte> buffer, sockaddr_storage& peer_addr, ::socklen_t& out_peer_addr_len,
                                         ip_address_t& out_peer_ip, port_t& out_peer_port) noexcept(false)
    {
        auto result = co_await recv(buffer, peer_addr, out_peer_addr_len);
        if (!result)
            co_return result;

        if (out_peer_addr_len < sizeof(sockaddr))
            co_return std::unexpected(error_from_errno(EINVAL));

        const auto* addr = reinterpret_cast<const sockaddr*>(&peer_addr);
        if (addr->sa_family == AF_INET)
        {
            if (out_peer_addr_len < sizeof(::sockaddr_in))
                co_return std::unexpected(error_from_errno(EINVAL));

            const auto* addr4 = reinterpret_cast<const ::sockaddr_in*>(&peer_addr);
            out_peer_ip = ipv4_address_t {reinterpret_cast<const std::uint8_t*>(&addr4->sin_addr), 4u};
            out_peer_port = ::ntohs(addr4->sin_port);
            co_return result;
        }

        if (addr->sa_family == AF_INET6)
        {
            if (out_peer_addr_len < sizeof(sockaddr_in6))
                co_return std::unexpected(error_from_errno(EINVAL));

            const auto* addr6 = reinterpret_cast<const sockaddr_in6*>(&peer_addr);
            out_peer_ip = ipv6_address_t {reinterpret_cast<const std::uint8_t*>(&addr6->sin6_addr), 16u};
            out_peer_port = ::ntohs(addr6->sin6_port);
            co_return result;
        }

        co_return std::unexpected(error_from_errno(EAFNOSUPPORT));
    }

    endpoint::result_task endpoint::send(std::span<const std::byte> buffer, const sockaddr* peer_addr,
                                         const ::socklen_t addr_len) noexcept(false)
    {
        if (peer_addr == nullptr)
            co_return std::unexpected(error_from_errno(EINVAL));

        iovec iov {};
        // iovec::iov_base is void* (non-const) by POSIX design; sendmsg does not modify the buffer.
        iov.iov_base = const_cast<void*>(static_cast<const void*>(buffer.data()));
        iov.iov_len = buffer.size();

        msghdr msg {};
        msg.msg_name = const_cast<sockaddr*>(peer_addr);
        msg.msg_namelen = addr_len;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        co_return co_await socket_.sendmsg(&msg);
    }

    endpoint::result_task endpoint::send(std::span<const std::byte> buffer, const ip_address_t peer_ip,
                                         const port_t peer_port) noexcept(false)
    {
        const auto peer_addr = make_socket_address(peer_ip, peer_port);
        if (!peer_addr)
            co_return std::unexpected(peer_addr.error());

        co_return co_await send(buffer, reinterpret_cast<const sockaddr*>(&peer_addr->storage), peer_addr->length);
    }
} // namespace kmx::aio::readiness::udp
