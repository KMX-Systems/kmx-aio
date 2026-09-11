/// @file src/kmx/aio/knx/secure/session_link.cpp
/// @brief The compiled body of the server session link.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/session_link.hpp>
#ifndef PCH
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/frame.hpp>

    #include <array>
#endif

namespace kmx::aio::knx::secure
{
    session_link::session_link(datagram_transport& connection, server_session_table& sessions, const std::uint16_t session_id) noexcept:
        connection_(connection),
        sessions_(sessions),
        session_id_(session_id)
    {
    }

    task_returning_expected_size_t session_link::send(const cspan_byte_t payload, const sockaddr*, const ::socklen_t) noexcept(false)
    {
        // The seal takes the next sequence number, so the send that follows it must go before any other seal of this session.
        const auto guard = co_await send_mutex_.lock();
        std::array<std::uint8_t, frame::max_datagram_size> wire {};
        const cspan_uint8_t plain {reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()};
        const auto sealed = sessions_.seal(&connection_, session_id_, plain, wire);
        if (!sealed.has_value())
            co_return std::unexpected(sealed.error());
        if (const auto sent = co_await send_wire({wire.data(), *sealed}); !sent.has_value())
            co_return std::unexpected(sent.error());
        co_return payload.size();
    }

    task_returning_expected_size_t session_link::receive(span_byte_t, transport_peer&) noexcept(false)
    {
        co_return std::unexpected(make_error_code(error::unsupported_service));
    }

    task_returning_expected_void_t session_link::send_status(const session_status status) noexcept(false)
    {
        const auto guard = co_await send_mutex_.lock();
        std::array<std::uint8_t, frame::max_datagram_size> wire {};
        const auto sealed = sessions_.seal_status(&connection_, session_id_, status, wire);
        if (!sealed.has_value())
            co_return std::unexpected(sealed.error());
        co_return co_await send_wire({wire.data(), *sealed});
    }

    task_returning_expected_void_t session_link::send_wire(const cspan_uint8_t wire) noexcept(false)
    {
        const auto sent = co_await connection_.send({reinterpret_cast<const std::byte*>(wire.data()), wire.size()}, nullptr, 0u);
        if (!sent.has_value())
            co_return std::unexpected(sent.error());
        if (*sent != wire.size())
            co_return std::unexpected(make_error_code(error::connection_failed));
        co_return expected_void_t {};
    }
}
