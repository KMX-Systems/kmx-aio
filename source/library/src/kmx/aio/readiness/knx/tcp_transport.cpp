/// @file kmx/aio/readiness/knx/tcp_transport.cpp
/// @brief The compiled body of the epoll KNX TCP transport.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/readiness/knx/tcp_transport.hpp>

#include <kmx/aio/error_code.hpp>
#include <kmx/aio/knx/error.hpp>
#include <kmx/aio/readiness/tcp/connect.hpp>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <system_error>
#include <utility>

namespace kmx::aio::readiness::knx
{
    namespace kn = kmx::aio::knx;

    /// @brief How long one wait lasts before the socket is asked again; long enough to cost nothing on an idle
    ///        connection, short enough that an edge lost before the wait subscribed delays a frame only briefly.
    static constexpr std::uint32_t recheck_interval_ms = 100u;

    [[nodiscard]] static std::uint32_t now_ms() noexcept
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    [[nodiscard]] static std::unexpected<std::error_code> refuse(const kn::error reason) noexcept
    {
        return std::unexpected(kn::make_error_code(reason));
    }

    tcp_transport::tcp_transport(executor& exec, const sockaddr* const peer, const ::socklen_t peer_length,
                                 const std::uint32_t connect_timeout_ms) noexcept:
        exec_(exec),
        connect_timeout_ms_(connect_timeout_ms)
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

        auto connected =
            co_await tcp::connect_until(exec_, reinterpret_cast<const sockaddr*>(&peer_), peer_length_, now_ms() + connect_timeout_ms_);
        if (!connected)
        {
            if (connected.error() == std::make_error_code(std::errc::timed_out))
                co_return refuse(kn::error::timeout);
            if (connected.error() == to_std_error_code(error_code::operation_cancelled))
                co_return std::unexpected(connected.error());
            co_return refuse(kn::error::connection_failed);
        }
        connection_ = std::move(*connected);
        reassembler_.reset();
        co_return expected_void_t {};
    }

    void tcp_transport::close() noexcept
    {
        if (!connection_.is_valid())
            return;
        // Shutting down tells the peer at once; unregistering resumes any wait here with a cancellation.
        static_cast<void>(::shutdown(connection_.get(), SHUT_RDWR));
        exec_.unregister_fd(connection_.get());
        connection_ = file_descriptor {};
        reassembler_.reset();
    }

    task_returning_expected_void_t tcp_transport::await_io(const event_type type, const optional_deadline_t deadline_ms) noexcept(false)
    {
        const auto now = now_ms();
        if (deadline_ms.has_value() && (static_cast<std::int32_t>(*deadline_ms - now) <= 0))
            co_return refuse(kn::error::timeout);
        if (!connection_.is_valid())
            co_return refuse(kn::error::shutdown);

        const auto slice = now + recheck_interval_ms;
        const auto until = (deadline_ms.has_value() && (static_cast<std::int32_t>(*deadline_ms - slice) < 0)) ? *deadline_ms : slice;
        if (co_await exec_.wait_io_until(connection_.get(), type, until) == executor::wait_status::cancelled)
            co_return std::unexpected(to_std_error_code(error_code::operation_cancelled));
        co_return expected_void_t {};
    }

    task_returning_expected_void_t tcp_transport::fill(const optional_deadline_t deadline_ms) noexcept(false)
    {
        for (;;)
        {
            if (!connection_.is_valid())
                co_return refuse(kn::error::shutdown);
            const auto space = reassembler_.writable();
            const auto received = ::recv(connection_.get(), space.data(), space.size(), MSG_DONTWAIT);
            const auto error_number = errno;
            if (received > 0)
            {
                reassembler_.commit(static_cast<std::size_t>(received));
                co_return expected_void_t {};
            }
            // An end in the middle of a frame truncates it; an end between frames is the peer closing the connection.
            if (received == 0)
                co_return refuse(reassembler_.partial() ? kn::error::malformed_frame : kn::error::shutdown);
            if ((error_number != EAGAIN) && (error_number != EWOULDBLOCK) && (error_number != EINTR))
                co_return std::unexpected(std::error_code(error_number, std::generic_category()));
            if (const auto waited = (error_number == EINTR) ? expected_void_t {} : co_await await_io(event_type::read, deadline_ms);
                !waited)
                co_return waited;
        }
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
            const auto written = ::send(connection_.get(), payload.data() + sent, payload.size() - sent, MSG_DONTWAIT | MSG_NOSIGNAL);
            const auto error_number = errno;
            if (written > 0)
                sent += static_cast<std::size_t>(written);
            else if ((written < 0) && (error_number != EAGAIN) && (error_number != EWOULDBLOCK) && (error_number != EINTR))
                co_return std::unexpected(std::error_code(error_number, std::generic_category()));
            else if (const auto waited = (error_number == EINTR) ? expected_void_t {} : co_await await_io(event_type::write, std::nullopt);
                     !waited)
                co_return std::unexpected(waited.error());
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
