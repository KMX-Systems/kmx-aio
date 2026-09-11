/// @file src/kmx/aio/readiness/udp/endpoint.cpp
/// @brief Readiness-model UDP endpoint: datagram receive, optionally with a deadline, and send to a peer address.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/readiness/udp/endpoint.hpp>
#ifndef PCH
    #include <netinet/in.h>
#endif

namespace kmx::aio::readiness::udp
{
    endpoint::expected_t endpoint::create(executor& exec, const int domain) noexcept
    {
        auto sock = socket::create(exec, domain);
        if (!sock)
            return std::unexpected(sock.error());

        return endpoint(std::move(*sock));
    }

    task_returning_expected_size_t endpoint::recv(span_byte_t buffer, sockaddr_storage& peer_addr,
                                                  ::socklen_t& out_peer_addr_len) noexcept(false)
    {
        iovec iov {};
        iov.iov_base = buffer.data();
        iov.iov_len = buffer.size();

        msghdr msg {};
        msg.msg_name = &peer_addr;
        msg.msg_namelen = sizeof(peer_addr);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1u;

        auto result = co_await socket_.recvmsg(&msg);
        if (result)
            out_peer_addr_len = msg.msg_namelen;

        co_return result;
    }

    task_returning_expected_size_t endpoint::recv_until(span_byte_t buffer, sockaddr_storage& peer_addr, ::socklen_t& out_peer_addr_len,
                                                        const std::uint32_t deadline_ms) noexcept(false)
    {
        out_peer_addr_len = 0u;
        iovec iov {buffer.data(), buffer.size()};
        msghdr msg {};
        msg.msg_name = &peer_addr;
        msg.msg_namelen = sizeof(peer_addr);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1u;
        const auto result = co_await socket_.recvmsg_until(&msg, deadline_ms);
        if (result)
            out_peer_addr_len = msg.msg_namelen;
        co_return result;
    }

    task_returning_expected_size_t endpoint::recv(span_byte_t buffer, socket_address& out_peer_address,
                                                  endpoint_address& out_peer) noexcept(false)
    {
        auto result = co_await recv(buffer, out_peer_address.storage, out_peer_address.length);
        if (!result)
            co_return result;

        const auto peer = parse_socket_address(out_peer_address);
        if (!peer)
            co_return std::unexpected(peer.error());

        out_peer = *peer;
        co_return result;
    }

    task_returning_expected_size_t endpoint::send(cspan_byte_t buffer, const sockaddr* peer_addr,
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
        msg.msg_iovlen = 1u;

        co_return co_await socket_.sendmsg(&msg);
    }

    task_returning_expected_size_t endpoint::send(cspan_byte_t buffer, const ip_address_t peer_ip, const port_t peer_port) noexcept(false)
    {
        const auto peer_addr = make_socket_address(peer_ip, peer_port);
        if (!peer_addr)
            co_return std::unexpected(peer_addr.error());

        co_return co_await send(buffer, reinterpret_cast<const sockaddr*>(&peer_addr->storage), peer_addr->length);
    }
}
