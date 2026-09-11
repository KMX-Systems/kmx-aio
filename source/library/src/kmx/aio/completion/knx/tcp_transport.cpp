/// @file src/kmx/aio/completion/knx/tcp_transport.cpp
/// @brief The compiled body of the io_uring KNX TCP transport.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/completion/knx/tcp_transport.hpp>
#ifndef PCH
    #include <kmx/aio/knx/error.hpp>

    #include <cerrno>
    #include <chrono>
    #include <cstring>
    #include <system_error>
    #include <utility>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <sys/uio.h>
#endif

namespace kmx::aio::completion::knx
{
    namespace kn = kmx::aio::knx;

    [[nodiscard]] static std::uint32_t now_ms() noexcept
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    [[nodiscard]] static std::unexpected<std::error_code> refuse(const kn::error reason) noexcept
    {
        return std::unexpected(kn::make_error_code(reason));
    }

    tcp_transport::tcp_transport(executor& exec, const sockaddr* const peer, const ::socklen_t peer_length) noexcept: exec_(exec)
    {
        if (kn::store_socket_address(peer_, peer, peer_length))
            peer_length_ = peer_length;
    }

    tcp_transport::tcp_transport(executor& exec, file_descriptor&& connection, const sockaddr_storage& peer,
                                 const ::socklen_t peer_length) noexcept:
        exec_(exec),
        peer_(peer),
        peer_length_(peer_length),
        connection_(std::move(connection))
    {
    }

    tcp_transport::~tcp_transport() noexcept
    {
        close();
    }

    task_returning_expected_void_t tcp_transport::open() noexcept(false)
    {
        if (connection_.is_valid())
            co_return expected_void_t {};
        if (peer_length_ == 0u)
            co_return refuse(kn::error::invalid_configuration);

        auto socket = file_descriptor::create_socket(peer_.ss_family, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (!socket)
            co_return refuse(kn::error::connection_failed);
        // Frames are small and a reply is waited for, which is the traffic Nagle's algorithm holds back.
        const int one = 1;
        static_cast<void>(::setsockopt(socket->get(), IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)));
        if (const auto connected = co_await exec_.async_connect(socket->get(), reinterpret_cast<const sockaddr*>(&peer_), peer_length_);
            !connected)
            co_return refuse(kn::error::connection_failed);

        connection_ = std::move(*socket);
        reassembler_.reset();
        co_return expected_void_t {};
    }

    void tcp_transport::close() noexcept
    {
        if (!connection_.is_valid())
            return;
        // Shutting the socket down completes any read or write still pending on it, with an end or an error.
        static_cast<void>(::shutdown(connection_.get(), SHUT_RDWR));
        connection_ = file_descriptor {};
        reassembler_.reset();
    }

    task_returning_expected_void_t tcp_transport::fill(const optional_deadline_t deadline_ms) noexcept(false)
    {
        if (!connection_.is_valid())
            co_return refuse(kn::error::shutdown);
        const auto remaining = deadline_ms.has_value() ? static_cast<std::int32_t>(*deadline_ms - now_ms()) : 1;
        if (remaining <= 0)
            co_return refuse(kn::error::timeout);

        const auto space = reassembler_.writable();
        ::iovec vector {space.data(), space.size()};
        ::msghdr message {};
        message.msg_iov = &vector;
        message.msg_iovlen = 1u;
        // Two statements, not a conditional expression: GCC evaluates around a co_await in either arm, and must not start
        // both receives.
        expected_size_t received {};
        if (deadline_ms.has_value())
            received = co_await exec_.async_recvmsg_until(connection_.get(), &message, static_cast<std::uint64_t>(remaining) * 1'000'000u);
        else
            received = co_await exec_.async_recvmsg(connection_.get(), &message);
        if (!received)
            co_return std::unexpected((received.error().value() == ETIMEDOUT) ? kn::make_error_code(kn::error::timeout) : received.error());
        // An end in the middle of a frame truncates it; an end between frames is the peer closing the connection.
        if (*received == 0u)
            co_return refuse(reassembler_.partial() ? kn::error::malformed_frame : kn::error::shutdown);
        reassembler_.commit(*received);
        co_return expected_void_t {};
    }

    expected_size_t tcp_transport::deliver(const cspan_uint8_t frame, const span_byte_t buffer, kn::transport_peer& peer) const noexcept
    {
        if (buffer.size() < frame.size())
            return refuse(kn::error::invalid_length);
        std::memcpy(buffer.data(), frame.data(), frame.size());
        peer.address = peer_;
        peer.length = peer_length_;
        return frame.size();
    }

    std::unexpected<std::error_code> tcp_transport::end_stream(const std::error_code failure) noexcept
    {
        // Only a deadline leaves the stream in step. After anything else nothing says where the next frame starts,
        // so the connection is closed, and the receives after this one report that it has ended.
        if (failure != kn::make_error_code(kn::error::timeout))
            close();
        return std::unexpected(failure);
    }

    task_returning_expected_size_t tcp_transport::receive_frame(const span_byte_t buffer, kn::transport_peer& peer,
                                                                const optional_deadline_t deadline_ms) noexcept(false)
    {
        for (;;)
        {
            const auto frame = reassembler_.next();
            if (!frame.has_value())
                co_return end_stream(frame.error());
            if (frame->has_value())
                co_return deliver(**frame, buffer, peer);
            if (const auto filled = co_await fill(deadline_ms); !filled)
                co_return end_stream(filled.error());
        }
    }

    task_returning_expected_size_t tcp_transport::send(const cspan_byte_t payload, const sockaddr*, const ::socklen_t) noexcept(false)
    {
        // One frame at a time: the octets of two frames must never interleave on the stream.
        const auto guard = co_await write_mutex_.lock();
        for (std::size_t sent {}; sent < payload.size();)
        {
            if (!connection_.is_valid())
                co_return refuse(kn::error::shutdown);
            ::iovec vector {const_cast<std::byte*>(payload.data() + sent), payload.size() - sent};
            ::msghdr message {};
            message.msg_iov = &vector;
            message.msg_iovlen = 1u;
            const auto written = co_await exec_.async_sendmsg(connection_.get(), &message, MSG_NOSIGNAL);
            if (!written)
                co_return std::unexpected(written.error());
            if (*written == 0u)
                co_return refuse(kn::error::connection_failed);
            sent += *written;
        }

        co_return payload.size();
    }

    task_returning_expected_size_t tcp_transport::receive(const span_byte_t buffer, kn::transport_peer& peer) noexcept(false)
    {
        co_return co_await receive_frame(buffer, peer, std::nullopt);
    }

    task_returning_expected_size_t tcp_transport::receive_until(const span_byte_t buffer, kn::transport_peer& peer,
                                                                const std::uint32_t deadline_ms) noexcept(false)
    {
        co_return co_await receive_frame(buffer, peer, deadline_ms);
    }
}
