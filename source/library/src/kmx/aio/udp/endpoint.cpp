#include "kmx/aio/udp/endpoint.hpp"

#include <sys/uio.h>

namespace kmx::aio::udp
{
    endpoint::create_result endpoint::create(executor& exec, const int domain) noexcept
    {
        auto sock = socket::create(exec, domain);
        if (!sock)
            return std::unexpected(sock.error());

        return endpoint(std::move(*sock));
    }

    endpoint::result_task endpoint::recv(std::span<std::byte> buffer, sockaddr_storage& peer_addr,
                                         socklen_t& out_peer_addr_len) noexcept(false)
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

    endpoint::result_task endpoint::send(std::span<const std::byte> buffer, const sockaddr* peer_addr,
                                         const socklen_t addr_len) noexcept(false)
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
} // namespace kmx::aio::udp
