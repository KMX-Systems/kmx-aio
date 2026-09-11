/// @file kmx/aio/knx/secure/tunnel_transport.cpp
/// @brief The compiled body of the KNX IP Secure tunnelling transport.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/tunnel_transport.hpp>

#include <kmx/aio/knx/datagram.hpp>
#include <kmx/aio/knx/discovery.hpp>
#include <kmx/aio/knx/error.hpp>

#include <algorithm>
#include <chrono>
#include <utility>
#include <variant>

namespace kmx::aio::knx::secure
{
    /// @brief The milliseconds of the steady clock, which the connection's deadlines are stamped in.
    [[nodiscard]] static std::uint32_t connection_now_ms() noexcept
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    /// @brief Indicates whether a service is discovery, which KNX IP Secure leaves unencrypted.
    [[nodiscard]] static constexpr bool discovery_service(const std::uint16_t service) noexcept
    {
        return (service == discovery::search_request_service) || (service == discovery::search_response_service) ||
               (service == discovery::description_request_service) || (service == discovery::description_response_service) ||
               (service == discovery::search_request_extended_service) || (service == discovery::search_response_extended_service);
    }

    /// @brief Indicates whether a failure ends the session, rather than refusing one frame.
    [[nodiscard]] static bool ends_session(const std::error_code failure) noexcept
    {
        return (failure == make_error_code(error::secure_session_closed)) || (failure == make_error_code(error::secure_session_rejected));
    }

    /// @brief Indicates whether octets announce a SECURE_WRAPPER, whether or not the rest of it could be read.
    [[nodiscard]] static bool announces_wrapper(const cspan_uint8_t wire) noexcept
    {
        return (wire.size() >= 4u) && (wire[2u] == static_cast<std::uint8_t>(secure_wrapper_service >> 8u)) &&
               (wire[3u] == static_cast<std::uint8_t>(secure_wrapper_service & 0xFFu));
    }

    tunnel_transport::tunnel_transport(datagram_transport& connection, tunnelling_credentials credentials,
                                       const monotonic_ms_function clock_ms, entropy_source& entropy) noexcept:
        connection_(connection),
        clock_ms_(clock_ms),
        session_(std::move(credentials), entropy)
    {
    }

    std::uint64_t tunnel_transport::now_ms() const noexcept
    {
        if (clock_ms_ != nullptr)
            return clock_ms_();
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    task_returning_expected_void_t tunnel_transport::open() noexcept(false)
    {
        if (established())
            co_return expected_void_t {};
        if (const auto opened = co_await connection_.open(); !opened.has_value())
            co_return opened;

        auto outcome = co_await handshake();
        // A handshake that fails leaves nothing behind: no key, and no connection a plain frame could follow on (P1).
        if (!outcome.has_value())
            close();
        co_return outcome;
    }

    void tunnel_transport::close() noexcept
    {
        with_session([](client_session& session) noexcept { session.close(); });
        connection_.close();
    }

    task_returning_expected_void_t tunnel_transport::handshake() noexcept(false)
    {
        std::array<std::uint8_t, session_request_size> request {};
        const auto now = now_ms();
        // Secure tunnelling runs over TCP, so the request names the TCP HPAI.
        const auto begun = with_session([&request, now](client_session& session) noexcept
                                        { return session.begin(request, hpai {ipv4_endpoint {}, 0x02u}, now); });
        if (!begun.has_value())
            co_return std::unexpected(begun.error());
        if (const auto sent = co_await send_wire(request); !sent.has_value())
            co_return sent;
        if (const auto authenticated = co_await await_response(connection_now_ms() + handshake_timeout_ms); !authenticated.has_value())
            co_return authenticated;
        co_return co_await await_status(connection_now_ms() + handshake_timeout_ms);
    }

    task_returning_expected_void_t tunnel_transport::await_response(const std::uint32_t deadline_ms) noexcept(false)
    {
        for (;;)
        {
            transport_peer peer {};
            const auto received = co_await receive_wire(peer, deadline_ms);
            if (!received.has_value())
                co_return std::unexpected(received.error());
            const auto decoded = decode_datagram({wire_.data(), *received});
            if (!decoded.has_value())
                continue;
            if (const auto* const response = std::get_if<session_response_frame>(&decoded->payload))
                co_return co_await authenticate(*response);
            // A server refuses in the clear before any key exists; anything else before the response is read past.
            if (const auto* const status = std::get_if<session_status_frame>(&decoded->payload))
                co_return with_session([status](client_session& session) noexcept { return session.on_unwrapped_status(*status); });
        }
    }

    task_returning_expected_void_t tunnel_transport::authenticate(const session_response_frame& response) noexcept(false)
    {
        std::array<std::uint8_t, frame::max_datagram_size> sealed {};
        const auto now = now_ms();
        const auto size = with_session([&response, &sealed, now](client_session& session) noexcept
                                       { return session.on_session_response(response, sealed, now); });
        if (!size.has_value())
            co_return std::unexpected(size.error());
        co_return co_await send_wire({sealed.data(), *size});
    }

    task_returning_expected_void_t tunnel_transport::await_status(const std::uint32_t deadline_ms) noexcept(false)
    {
        std::array<std::uint8_t, frame::max_datagram_size> plain {};
        while (!established())
        {
            transport_peer peer {};
            const auto received = co_await receive_wire(peer, deadline_ms);
            if (!received.has_value())
                co_return std::unexpected(received.error());
            // Only the end of the session stops the wait; a frame refused along the way is counted and read past.
            if (const auto unwrapped = unwrap(*received, plain); !unwrapped.has_value() && ends_session(unwrapped.error()))
                co_return std::unexpected(unwrapped.error());
        }
        co_return expected_void_t {};
    }

    task_returning_expected_size_t tunnel_transport::receive_wire(transport_peer& peer,
                                                                  const optional_deadline_t deadline_ms) noexcept(false)
    {
        const span_byte_t buffer {reinterpret_cast<std::byte*>(wire_.data()), wire_.size()};
        // Not a conditional expression: GCC evaluates operands of both arms around a co_await, and would read the deadline
        // a receive without one does not have.
        if (deadline_ms.has_value())
            co_return co_await connection_.receive_until(buffer, peer, *deadline_ms);
        co_return co_await connection_.receive(buffer, peer);
    }

    tunnel_transport::unwrapped_t tunnel_transport::unwrap(const std::size_t size, const span_uint8_t plain) noexcept
    {
        const cspan_uint8_t wire {wire_.data(), size};
        const auto decoded = decode_datagram(wire);
        const auto now = now_ms();
        const std::lock_guard lock {session_mutex_};
        if (!decoded.has_value())
        {
            // A wrapper too short to read never reaches the session, but is counted as one that did not authenticate.
            if (announces_wrapper(wire))
                session_.note_unauthenticated();
            return std::optional<std::size_t> {};
        }
        if (const auto* const wrapper = std::get_if<secure_wrapper_frame>(&decoded->payload))
        {
            const auto opened = session_.open(*wrapper, plain, now);
            if (!opened.has_value())
                return ends_session(opened.error()) ? unwrapped_t {std::unexpected(opened.error())} : std::optional<std::size_t> {};
            return opened->for_tunnel ? std::optional<std::size_t> {opened->size} : std::optional<std::size_t> {};
        }
        if (discovery_service(decoded->service_type) && (plain.size() >= size))
        {
            std::ranges::copy(wire, plain.begin());
            return std::optional<std::size_t> {size};
        }
        if (const auto* const status = std::get_if<session_status_frame>(&decoded->payload))
            return std::unexpected(session_.on_unwrapped_status(*status).error());
        session_.note_unencrypted();
        return std::unexpected(make_error_code(error::secure_frame_required));
    }

    task_returning_expected_size_t tunnel_transport::receive_frame(const span_byte_t buffer, transport_peer& peer,
                                                                   const optional_deadline_t deadline_ms) noexcept(false)
    {
        const span_uint8_t plain {reinterpret_cast<std::uint8_t*>(buffer.data()), buffer.size()};
        for (;;)
        {
            const auto received = co_await receive_wire(peer, deadline_ms);
            if (!received.has_value())
                co_return std::unexpected(received.error());
            const auto unwrapped = unwrap(*received, plain);
            // The end of the session is the end of the connection too.
            if (!unwrapped.has_value() && ends_session(unwrapped.error()))
                connection_.close();
            if (!unwrapped.has_value())
                co_return std::unexpected(unwrapped.error());
            if (unwrapped->has_value())
                co_return **unwrapped;
        }
    }

    task_returning_expected_size_t tunnel_transport::receive(const span_byte_t buffer, transport_peer& peer) noexcept(false)
    {
        co_return co_await receive_frame(buffer, peer, std::nullopt);
    }

    task_returning_expected_size_t tunnel_transport::receive_until(const span_byte_t buffer, transport_peer& peer,
                                                                   const std::uint32_t deadline_ms) noexcept(false)
    {
        co_return co_await receive_frame(buffer, peer, deadline_ms);
    }

    task_returning_expected_void_t tunnel_transport::send_locked(const cspan_uint8_t wire) noexcept(false)
    {
        const auto sent = co_await connection_.send({reinterpret_cast<const std::byte*>(wire.data()), wire.size()}, nullptr, 0u);
        if (!sent.has_value())
            co_return std::unexpected(sent.error());
        co_return expected_void_t {};
    }

    task_returning_expected_void_t tunnel_transport::send_wire(const cspan_uint8_t wire) noexcept(false)
    {
        const auto turn = co_await send_mutex_.lock();
        co_return co_await send_locked(wire);
    }

    task_returning_expected_size_t tunnel_transport::send(const cspan_byte_t payload, const sockaddr*, const ::socklen_t) noexcept(false)
    {
        const cspan_uint8_t plain {reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()};
        // Sealing and sending under one lock puts wrappers on the connection in the order of their sequence numbers.
        const auto turn = co_await send_mutex_.lock();
        std::array<std::uint8_t, frame::max_datagram_size> sealed {};
        const auto now = now_ms();
        const auto size =
            with_session([plain, &sealed, now](client_session& session) noexcept { return session.seal(plain, sealed, now); });
        if (!size.has_value())
            co_return std::unexpected(size.error());
        if (const auto sent = co_await send_locked({sealed.data(), *size}); !sent.has_value())
            co_return std::unexpected(sent.error());
        co_return payload.size();
    }

    task_returning_expected_void_t tunnel_transport::keep_alive() noexcept(false)
    {
        const auto turn = co_await send_mutex_.lock();
        std::array<std::uint8_t, frame::max_datagram_size> sealed {};
        const auto now = now_ms();
        const auto size =
            with_session([&sealed, now](client_session& session) noexcept { return session.prepare_keep_alive(sealed, now); });
        if (!size.has_value())
            co_return std::unexpected(size.error());
        co_return co_await send_locked({sealed.data(), *size});
    }

    task_returning_expected_void_t tunnel_transport::end_session() noexcept(false)
    {
        const auto turn = co_await send_mutex_.lock();
        std::array<std::uint8_t, frame::max_datagram_size> sealed {};
        const auto now = now_ms();
        const auto size = with_session(
            [&sealed, now](client_session& session) noexcept -> expected_size_t
            {
                // Without a session key there is no session to close politely, only one to forget.
                if (!session.established())
                {
                    session.close();
                    return 0u;
                }
                return session.prepare_close(sealed, now);
            });
        if (!size.has_value())
            co_return std::unexpected(size.error());
        if (*size == 0u)
            co_return expected_void_t {};
        co_return co_await send_locked({sealed.data(), *size});
    }

    bool tunnel_transport::keep_alive_due() const noexcept
    {
        const auto now = now_ms();
        return with_session([now](const client_session& session) noexcept { return session.keep_alive_due(now); });
    }

    expected_void_t tunnel_transport::check_timeout() noexcept
    {
        const auto now = now_ms();
        const auto live = with_session([now](client_session& session) noexcept { return session.check_timeout(now); });
        if (!live.has_value())
            connection_.close();
        return live;
    }

    bool tunnel_transport::established() const noexcept
    {
        return with_session([](const client_session& session) noexcept { return session.established(); });
    }

    statistics tunnel_transport::counters() const noexcept
    {
        return with_session([](const client_session& session) noexcept { return session.counters(); });
    }
}
