/// @file src/kmx/aio/knx/tunnelling_client.cpp
/// @brief The compiled body of the KNXnet/IP tunnelling client: connect, send, receive, heartbeat and disconnect.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/tunnelling_client.hpp>
#ifndef PCH
    #include <kmx/aio/knx/data_secure/context.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/secure/entropy.hpp>
    #include <kmx/aio/knx/secure/tunnel_transport.hpp>
    #include <kmx/aio/promise_base.hpp>

    #include <chrono>
    #include <cstddef>
    #include <cstring>
    #include <optional>
    #include <variant>
    #include <netinet/in.h>
#endif

namespace kmx::aio::knx
{
    namespace detail
    {
        /// @brief Largest cEMI message this client will put on the wire.
        /// @details Bounded by what a cEMI decoder can accept, not by what the datagram buffer can hold.
        ///          The buffer would take 1462 octets, but the cEMI length fields cannot describe a message
        ///          longer than @ref kmx::aio::knx::cemi::max_message_size - so anything above that encodes
        ///          into a frame the peer, this library's own decoder included, must reject. Accepting it
        ///          here only moves the failure to the far end of the link.
        constexpr std::size_t max_cemi_size = cemi::max_message_size;
        static_assert(max_cemi_size <=
                          (frame::max_datagram_size - frame::communication_header_size - frame::tunnelling_request_header_size),
                      "a sendable cEMI message must still fit one buffered datagram");

        /// @brief The HPAI host protocol of KNXnet/IP over UDP.
        constexpr std::uint8_t udp_host_protocol = 0x01u;
        /// @brief The HPAI host protocol of KNXnet/IP over TCP.
        constexpr std::uint8_t tcp_host_protocol = 0x02u;

        /// @brief Returns how many octets an encoded KNXnet/IP frame spans, as its header states.
        /// @param packet An encoded frame, at least a header long.
        [[nodiscard]] constexpr std::size_t encoded_frame_length(const cspan_uint8_t packet) noexcept
        {
            return static_cast<std::size_t>((static_cast<std::uint16_t>(packet[4u]) << 8u) | packet[5u]);
        }
    }

    tunnelling_client::operation_guard::operation_guard(tunnelling_client& owner) noexcept: owner_(&owner)
    {
        bool expected {};
        acquired_ = owner.operation_active_.compare_exchange_strong(expected, true, std::memory_order_acquire, std::memory_order_relaxed);
    }

    tunnelling_client::operation_guard::~operation_guard() noexcept
    {
        if (acquired_)
            owner_->operation_active_.store(false, std::memory_order_release);
    }

    tunnelling_client::tunnelling_client(datagram_transport& transport, const sockaddr* const peer, const ::socklen_t peer_length,
                                         const tunnelling_client_options options) noexcept:
        transport_(transport),
        peer_length_(peer_length),
        clock_now_(options.clock_now),
        session_(options.config)
    {
        configure(peer);
    }

    tunnelling_client::tunnelling_client(datagram_transport& transport, const sockaddr* const peer, const ::socklen_t peer_length,
                                         secure_tunnelling_options options) noexcept(false):
        secure_(std::make_unique<secure::tunnel_transport>(transport, std::move(options.credentials), options.clock_ms,
                                                           (options.entropy != nullptr) ? *options.entropy : secure::system_entropy())),
        transport_(*secure_),
        peer_length_(peer_length),
        clock_now_(options.tunnel.clock_now),
        session_(options.tunnel.config)
    {
        configure(peer);
    }

    tunnelling_client::~tunnelling_client() noexcept = default;

    void tunnelling_client::configure(const sockaddr* const peer) noexcept
    {
        // Only peer_length octets are read: peer may point at a sockaddr_in, which is an eighth the size of
        // the sockaddr_storage it is stored into.
        configured_peer_valid_ = store_socket_address(peer_, peer, peer_length_);
        if (configured_peer_valid_ && (peer_.ss_family == AF_INET))
            configured_peer_valid_ = peer_length_ >= sizeof(sockaddr_in);
        else if (configured_peer_valid_ && (peer_.ss_family == AF_INET6))
            configured_peer_valid_ = peer_length_ >= sizeof(sockaddr_in6);
        // The transport decides which rules the tunnel follows: over a stream there is nothing to acknowledge.
        session_.use_stream_rules(transport_.stream_oriented());
    }

    /// @brief Compares two IPv4 endpoints, address and port alike.
    /// @param expected_peer The endpoint this client is bound to.
    /// @param expected_length How many octets of @p expected_peer are meaningful.
    /// @param peer The endpoint a datagram arrived from.
    /// @return `true` when both name the same address and port.
    [[nodiscard]] static bool same_ipv4(const sockaddr_storage& expected_peer, const ::socklen_t expected_length,
                                        const transport_peer& peer) noexcept
    {
        if ((expected_length < sizeof(sockaddr_in)) || (peer.length < sizeof(sockaddr_in)))
            return false;
        const auto& expected = reinterpret_cast<const sockaddr_in&>(expected_peer);
        const auto& actual = reinterpret_cast<const sockaddr_in&>(peer.address);
        return (expected.sin_port == actual.sin_port) && (expected.sin_addr.s_addr == actual.sin_addr.s_addr);
    }

    /// @copydoc same_ipv4
    /// @note The flow label and scope id are compared too: a link-local address is not identified by its
    ///       octets alone, since the same address on another interface is a different endpoint.
    [[nodiscard]] static bool same_ipv6(const sockaddr_storage& expected_peer, const ::socklen_t expected_length,
                                        const transport_peer& peer) noexcept
    {
        if ((expected_length < sizeof(sockaddr_in6)) || (peer.length < sizeof(sockaddr_in6)))
            return false;
        const auto& expected = reinterpret_cast<const sockaddr_in6&>(expected_peer);
        const auto& actual = reinterpret_cast<const sockaddr_in6&>(peer.address);
        return (expected.sin6_port == actual.sin6_port) && (expected.sin6_flowinfo == actual.sin6_flowinfo) &&
               (expected.sin6_scope_id == actual.sin6_scope_id) &&
               (std::memcmp(&expected.sin6_addr, &actual.sin6_addr, sizeof(expected.sin6_addr)) == 0);
    }

    bool tunnelling_client::peer_matches(const transport_peer& peer, const endpoint_kind kind) const noexcept
    {
        const auto& expected_peer = (kind == endpoint_kind::data) ? data_peer_ : peer_;
        const auto expected_length = (kind == endpoint_kind::data) ? data_peer_length_ : peer_length_;
        if ((kind == endpoint_kind::data) && !data_peer_valid_)
            return false;

        if ((expected_length == 0u) || (expected_length > sizeof(sockaddr_storage)) || (peer.length == 0u) ||
            (peer.length > sizeof(sockaddr_storage)) ||
            ((expected_peer.ss_family != AF_UNSPEC) && (peer.address.ss_family != expected_peer.ss_family)))
            return false;
        if ((kind == endpoint_kind::control) && (expected_peer.ss_family == AF_UNSPEC))
            return (peer.address.ss_family == AF_UNSPEC) || ((peer.address.ss_family == AF_INET) && (peer.length >= sizeof(sockaddr_in)));

        switch (expected_peer.ss_family)
        {
            case AF_INET:
                return same_ipv4(expected_peer, expected_length, peer);
            case AF_INET6:
                return same_ipv6(expected_peer, expected_length, peer);
            default:
                return (peer.length == expected_length) && (std::memcmp(&peer.address, &expected_peer, expected_length) == 0);
        }
    }

    std::uint32_t tunnelling_client::now_ms() const noexcept
    {
        if (clock_now_ != nullptr)
            return clock_now_();

        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    std::uint32_t tunnelling_client::operation_deadline_ms() const noexcept
    {
        return now_ms() + session_.ack_timeout_ms();
    }

    std::uint32_t tunnelling_client::connect_deadline_ms() const noexcept
    {
        return now_ms() + session_.connect_timeout_ms();
    }

    std::uint32_t tunnelling_client::disconnect_deadline_ms() const noexcept
    {
        return now_ms() + session_.disconnect_timeout_ms();
    }

    std::uint32_t tunnelling_client::connectionstate_deadline_ms() const noexcept
    {
        return now_ms() + session_.connectionstate_timeout_ms();
    }

    task_returning_expected_void_t tunnelling_client::send_packet(const cspan_uint8_t packet, const endpoint_kind kind) noexcept(false)
    {
        const auto& destination = (kind == endpoint_kind::data) ? data_peer_ : peer_;
        const auto destination_length = (kind == endpoint_kind::data) ? data_peer_length_ : peer_length_;
        if (!configured_peer_valid_ || (destination_length == 0u) || (destination_length > sizeof(sockaddr_storage)) ||
            ((kind == endpoint_kind::data) && !data_peer_valid_))
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        const auto* bytes = reinterpret_cast<const std::byte*>(packet.data());
        const auto result = co_await transport_.send(cspan_byte_t {bytes, packet.size()}, reinterpret_cast<const sockaddr*>(&destination),
                                                     destination_length);
        if (!result)
            co_return std::unexpected(result.error());
        if (result.value() != packet.size())
            co_return std::unexpected(make_error_code(error::connection_failed));
        co_return expected_void_t {};
    }

    bool tunnelling_client::is_retransmission(const expected_void_t& accepted, const tunnelling_request_frame& request) const noexcept
    {
        // A repeat of the frame just seen: the server is retrying because it missed the acknowledgement,
        // not because it has anything new to deliver. It is acknowledged again and dropped.
        return !accepted.has_value() && (accepted.error() == make_error_code(error::sequence_error)) &&
               session_.duplicate_indication(request);
    }

    tunnelling_client::indication_outcome tunnelling_client::accept_indication(const datagram& value,
                                                                               const tunnelling_request_frame& request) noexcept
    {
        const auto now = now_ms();
        const std::lock_guard lock {session_mutex_};
        auto accepted = session_.dispatch_session_datagram(value, now);
        const auto duplicate = is_retransmission(accepted, request);
        return {std::move(accepted), duplicate};
    }

    expected_void_t tunnelling_client::check_session_live() noexcept
    {
        const auto now = now_ms();
        return with_session(
            [now](tunnelling_session& session) noexcept -> expected_void_t
            {
                if (session.state() == session_state::closed)
                    return std::unexpected(make_error_code(error::shutdown));
                return session.check_inactivity(now);
            });
    }

    expected_void_t tunnelling_client::absorb_session_datagram(const datagram& value) noexcept
    {
        // A heartbeat answer advances the session; an acknowledgement for a frame this client already
        // stopped waiting on - or, over a stream, one nothing waits on at all - is simply dropped. Anything
        // else is not ours to receive here.
        if (std::holds_alternative<connectionstate_response_frame>(value.payload))
            return with_session([&value](tunnelling_session& session) noexcept { return session.on_datagram(value); });
        if (std::holds_alternative<tunnelling_ack_frame>(value.payload))
            return {};
        return std::unexpected(make_error_code(error::unsupported_service));
    }

    tunnelling_client::endpoint_kind tunnelling_client::endpoint_for(const std::uint16_t service_type) const noexcept
    {
        // Only the services that travel the data channel are expected from the data peer, and only once
        // one has been learned; everything else is control traffic.
        const auto on_data_channel = (service_type == frame::tunnelling_request_service) || (service_type == frame::tunnelling_ack_service);
        return (on_data_channel && data_peer_valid_) ? endpoint_kind::data : endpoint_kind::control;
    }

    task_returning_expected_void_t tunnelling_client::send_data_packet(const cspan_uint8_t packet) noexcept(false)
    {
        co_return co_await send_packet(packet, endpoint_kind::data);
    }

    task_returning_expected_void_t tunnelling_client::acknowledge(const tunnelling_request_frame& request) noexcept(false)
    {
        // Over a stream nothing is acknowledged: the connection has already delivered the frame, once.
        if (transport_.stream_oriented())
            co_return expected_void_t {};

        std::array<std::uint8_t, frame::communication_header_size + frame::tunnelling_ack_size> ack {};
        if (const auto prepared = session_.prepare_tunnelling_ack_packet(ack, request); !prepared.has_value())
            co_return std::unexpected(prepared.error());
        co_return co_await send_data_packet(ack);
    }

    task_returning_expected_void_t tunnelling_client::retry_request(const cspan_uint8_t packet) noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        for (;;)
        {
            if (stop_token.stop_requested())
                co_return std::unexpected(make_error_code(error::shutdown));
            if (const auto sent = co_await send_packet(packet, endpoint_kind::data); !sent)
                co_return sent;

            const auto deadline = with_session([](const tunnelling_session& session) noexcept { return session.deadline_ms(); });
            const auto received = co_await receive_into_session(endpoint_kind::data, deadline);
            if (received.has_value())
                co_return received;
            if (received.error() == make_error_code(error::sequence_error))
                continue;
            if (received.error() != make_error_code(error::timeout))
                co_return std::unexpected(received.error());

            if (const auto retry = with_session([](tunnelling_session& session) noexcept { return session.on_timeout(); });
                !retry.has_value())
                co_return std::unexpected(retry.error());
        }
    }

    task_returning_expected_void_t tunnelling_client::retry_heartbeat(const cspan_uint8_t packet) noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        for (;;)
        {
            if (stop_token.stop_requested())
                co_return std::unexpected(make_error_code(error::shutdown));
            if (const auto sent = co_await send_packet(packet, endpoint_kind::control); !sent)
                co_return sent;

            // Recomputed per attempt, and from the connection-state timeout rather than from whatever
            // deadline the previous operation happened to leave behind in the session.
            const auto received = co_await receive_into_session(endpoint_kind::control, connectionstate_deadline_ms());
            if (received.has_value())
                co_return received;
            if (received.error() == make_error_code(error::sequence_error))
                continue;
            if (received.error() != make_error_code(error::timeout))
                co_return std::unexpected(received.error());

            // A refused heartbeat is retried rather than reported: the session decides when the run of
            // them has gone on long enough to call the connection lost.
            if (const auto timeout =
                    with_session([](tunnelling_session& session) noexcept { return session.on_connectionstate_timeout(); });
                !timeout.has_value() && (timeout.error() != make_error_code(error::connection_failed)))
                co_return std::unexpected(timeout.error());
        }
    }

    task_returning_expected_void_t tunnelling_client::await_disconnect_response(const cspan_uint8_t packet) noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        // Sent again only when an attempt times out. Traffic that arrives first - a heartbeat's answer, say - is read
        // past, not taken as a reason to repeat a request the server may already have acted on.
        auto sent = co_await send_packet(packet, endpoint_kind::control);
        auto deadline = disconnect_deadline_ms();
        while (sent.has_value())
        {
            if (stop_token.stop_requested())
                co_return std::unexpected(make_error_code(error::shutdown));

            transport_peer peer {};
            auto* bytes = reinterpret_cast<std::byte*>(receive_buffer_.data());
            const auto received = co_await transport_.receive_until(span_byte_t {bytes, receive_buffer_.size()}, peer, deadline);
            if (received.has_value())
            {
                if (const auto answer = on_disconnect_datagram(peer, received.value()); answer.has_value())
                    co_return *answer;
                continue;
            }

            if (received.error() != make_error_code(error::timeout))
                co_return std::unexpected(received.error());
            if (const auto retry = with_session([](tunnelling_session& session) noexcept { return session.on_disconnect_timeout(); });
                !retry.has_value())
                co_return std::unexpected(retry.error());
            sent = co_await send_packet(packet, endpoint_kind::control);
            deadline = disconnect_deadline_ms();
        }

        co_return sent;
    }

    optional_expected_void_t tunnelling_client::on_disconnect_datagram(const transport_peer& peer, const std::size_t size) noexcept
    {
        if (!peer_matches(peer, endpoint_kind::control))
            return expected_void_t {std::unexpected(make_error_code(error::connection_failed))};
        if ((size == 0u) || (size > receive_buffer_.size()))
            return expected_void_t {std::unexpected(make_error_code(error::invalid_length))};

        const auto decoded = decode_datagram({receive_buffer_.data(), size});
        if (!decoded.has_value())
            return expected_void_t {std::unexpected(decoded.error())};
        // Anything else on the control channel while closing is ignored, not treated as the answer.
        if (decoded->service_type != connection::disconnect_response_service)
            return {};
        return with_session([&decoded](tunnelling_session& session) noexcept { return session.on_datagram(decoded.value()); });
    }

    task_returning_expected_void_t tunnelling_client::retry_connect(const cspan_uint8_t packet) noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        for (;;)
        {
            if (stop_token.stop_requested())
                co_return std::unexpected(make_error_code(error::shutdown));
            if (const auto sent = co_await send_packet(packet, endpoint_kind::control); !sent)
                co_return sent;

            const auto deadline = with_session([](const tunnelling_session& session) noexcept { return session.deadline_ms(); });
            const auto received = co_await receive_into_session(endpoint_kind::control, deadline);
            if (received.has_value())
                co_return received;
            if (received.error() != make_error_code(error::timeout))
                co_return std::unexpected(received.error());

            if (const auto retry = with_session([](tunnelling_session& session) noexcept { return session.on_connect_timeout(); });
                !retry.has_value())
                co_return std::unexpected(retry.error());
        }
    }

    void tunnelling_client::adopt_data_peer(const hpai& endpoint) noexcept
    {
        data_peer_ = {};
        auto& address = reinterpret_cast<sockaddr_in&>(data_peer_);
        address.sin_family = AF_INET;
        address.sin_port = htons(endpoint.endpoint.port);
        std::memcpy(&address.sin_addr.s_addr, endpoint.endpoint.address.data(), endpoint.endpoint.address.size());
        data_peer_length_ = sizeof(sockaddr_in);
        data_peer_valid_ = true;
    }

    void tunnelling_client::adopt_data_peer(const ipv6_hpai& endpoint) noexcept
    {
        data_peer_ = {};
        auto& address = reinterpret_cast<sockaddr_in6&>(data_peer_);
        address.sin6_family = AF_INET6;
        address.sin6_port = htons(endpoint.endpoint.port);
        std::memcpy(&address.sin6_addr, endpoint.endpoint.address.data(), endpoint.endpoint.address.size());
        data_peer_length_ = sizeof(sockaddr_in6);
        data_peer_valid_ = true;
    }

    expected_void_t tunnelling_client::adopt_ipv4_data_endpoint(const hpai& endpoint) noexcept
    {
        const auto stream = transport_.stream_oriented();
        if (endpoint.protocol != (stream ? detail::tcp_host_protocol : detail::udp_host_protocol))
            return std::unexpected(make_error_code(error::unsupported_hpai));
        // Over a stream the connection is the data channel too; over UDP the server may name no data endpoint of
        // its own. Either way its control endpoint stays in use.
        if (stream || route_back(endpoint))
        {
            data_peer_ = peer_;
            data_peer_length_ = peer_length_;
            data_peer_valid_ = configured_peer_valid_;
            return {};
        }

        if (endpoint.endpoint.port == 0u)
            return std::unexpected(make_error_code(error::unsupported_hpai));

        adopt_data_peer(endpoint);
        return {};
    }

    expected_void_t tunnelling_client::adopt_data_endpoint(const datagram& value) noexcept
    {
        // A refusal names no endpoint worth adopting; the session reports the refusal itself.
        if (const auto* response = std::get_if<connect_response_frame>(&value.payload))
            return (response->status == connect_status::no_error) ? adopt_ipv4_data_endpoint(response->data_endpoint) : expected_void_t {};
        if (const auto* response = std::get_if<ipv6_connect_response_frame>(&value.payload))
        {
            const auto& endpoint = response->data_endpoint;
            if ((endpoint.protocol != detail::udp_host_protocol) || (endpoint.endpoint.port == 0u))
                return std::unexpected(make_error_code(error::unsupported_hpai));

            adopt_data_peer(endpoint);
        }

        return {};
    }

    task_returning_expected_void_t tunnelling_client::receive_into_session(const endpoint_kind kind,
                                                                           const std::uint32_t deadline_ms) noexcept(false)
    {
        transport_peer peer {};
        auto* bytes = reinterpret_cast<std::byte*>(receive_buffer_.data());
        const auto result = co_await transport_.receive_until(span_byte_t {bytes, receive_buffer_.size()}, peer, deadline_ms);
        if (!result)
            co_return std::unexpected(result.error());
        if (!peer_matches(peer, kind))
            co_return std::unexpected(make_error_code(error::connection_failed));
        if (result.value() > receive_buffer_.size())
            co_return std::unexpected(make_error_code(error::invalid_length));

        const auto decoded = decode_datagram({receive_buffer_.data(), result.value()});
        if (!decoded.has_value())
            co_return std::unexpected(decoded.error());

        if (const auto adopted = adopt_data_endpoint(decoded.value()); !adopted.has_value())
            co_return std::unexpected(adopted.error());

        const auto now = now_ms();
        co_return with_session([&decoded, now](tunnelling_session& session) noexcept
                               { return session.dispatch_session_datagram(decoded.value(), now); });
    }

    /// @brief Checks both endpoints of a connect request are usable.
    /// @tparam Endpoint The HPAI type, which differs between the IPv4 and IPv6 requests.
    /// @param control The control endpoint the request names.
    /// @param data The data endpoint the request names.
    /// @param over_stream Whether the request would travel on a stream, whose tunnels are asked for with the IPv4
    ///        TCP HPAI alone.
    /// @return Nothing, or why the pair cannot be connected with.
    /// @details An all-zero HPAI is the route-back form, not a misconfigured one: it asks the server to
    ///          answer the source of the datagram, which is the only way a client behind NAT is reachable.
    template <typename Endpoint>
    [[nodiscard]] static expected_void_t validate_connect_endpoints(const Endpoint& control, const Endpoint& data,
                                                                    const bool over_stream) noexcept
    {
        if (over_stream || (control.protocol != detail::udp_host_protocol) || (data.protocol != detail::udp_host_protocol))
            return std::unexpected(make_error_code(error::unsupported_hpai));
        if (((control.endpoint.port == 0u) && !route_back(control)) || ((data.endpoint.port == 0u) && !route_back(data)))
            return std::unexpected(make_error_code(error::invalid_configuration));
        return {};
    }

    task_returning_expected_void_t tunnelling_client::connect(const connect_request_frame& request) noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested())
            co_return std::unexpected(make_error_code(error::shutdown));
        operation_guard operation {*this};
        if (!operation.acquired())
            co_return std::unexpected(make_error_code(error::send_queue_full));
        // KNX IP Secure tunnelling is offered over TCP alone, so a secure client needs a stream underneath.
        if ((secure_ != nullptr) && !transport_.stream_oriented())
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        if (transport_.stream_oriented())
            co_return co_await connect_stream(request);
        if (const auto valid = validate_connect_endpoints(request.control_endpoint, request.data_endpoint, false); !valid.has_value())
            co_return std::unexpected(valid.error());

        clear_data_peer();
        co_return co_await start_and_retry_connect(request);
    }

    task_returning_expected_void_t tunnelling_client::start_and_retry_connect(const connect_request_frame& request) noexcept(false)
    {
        // Room for the extended CRI, which a request naming the address to tunnel under carries.
        std::array<std::uint8_t, frame::communication_header_size + connection::extended_connect_request_body_size> packet {};
        const auto deadline = connect_deadline_ms();
        if (const auto started = with_session([&packet, &request, deadline](tunnelling_session& session) noexcept
                                              { return session.start_connect(packet, request, deadline); });
            !started.has_value())
            co_return std::unexpected(started.error());

        co_return co_await retry_connect({packet.data(), detail::encoded_frame_length(packet)});
    }

    task_returning_expected_void_t tunnelling_client::connect_stream(const connect_request_frame& request) noexcept(false)
    {
        // The connection is both endpoints of a tunnel over TCP, so the request names neither: each HPAI is the TCP
        // one, with no address and no port.
        auto tcp_request = request;
        tcp_request.control_endpoint = hpai {{}, detail::tcp_host_protocol};
        tcp_request.data_endpoint = tcp_request.control_endpoint;
        clear_data_peer();
        if (const auto opened = co_await transport_.open(); !opened.has_value())
            co_return opened;

        auto connected = co_await start_and_retry_connect(tcp_request);
        // A connection that did not become a tunnel is not kept: the next attempt starts on a new one.
        if (!connected.has_value())
            transport_.close();
        co_return connected;
    }

    task_returning_expected_void_t tunnelling_client::connect(const ipv6_connect_request_frame& request) noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested())
            co_return std::unexpected(make_error_code(error::shutdown));
        operation_guard operation {*this};
        if (!operation.acquired())
            co_return std::unexpected(make_error_code(error::send_queue_full));
        if (const auto valid = validate_connect_endpoints(request.control_endpoint, request.data_endpoint, transport_.stream_oriented());
            !valid.has_value())
            co_return std::unexpected(valid.error());

        clear_data_peer();
        std::array<std::uint8_t, 50u> packet {};
        if (const auto result = connection::encode_ipv6_connect_request_packet(packet, request); !result.has_value())
            co_return std::unexpected(result.error());
        const auto deadline = connect_deadline_ms();
        if (const auto result = with_session([&packet, deadline](tunnelling_session& session) noexcept
                                             { return session.start_connect_raw(packet, deadline); });
            !result.has_value())
            co_return std::unexpected(result.error());

        co_return co_await retry_connect({packet.data(), 50u});
    }

    std::expected<std::uint8_t, std::error_code> tunnelling_client::prepare_request(const span_uint8_t packet,
                                                                                    const cspan_uint8_t cemi_bytes) noexcept
    {
        const auto deadline = operation_deadline_ms();
        return with_session(
            [packet, cemi_bytes, deadline](tunnelling_session& session) noexcept
            { return session.prepare_request_packet(packet, static_cast<std::uint8_t>(session.channel_id()), cemi_bytes, deadline); });
    }

    task_returning_expected_void_t tunnelling_client::send(const cspan_uint8_t cemi_bytes) noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested())
            co_return std::unexpected(make_error_code(error::shutdown));
        if (transport_.stream_oriented())
            co_return co_await send_on_stream(cemi_bytes);
        operation_guard operation {*this};
        if (!operation.acquired())
            co_return std::unexpected(make_error_code(error::send_queue_full));

        if (cemi_bytes.size() > detail::max_cemi_size)
            co_return std::unexpected(make_error_code(error::payload_too_large));

        std::array<std::uint8_t, frame::max_datagram_size> packet {};
        if (const auto sequence = prepare_request(packet, cemi_bytes); !sequence.has_value())
            co_return std::unexpected(sequence.error());

        co_return co_await retry_request({packet.data(), detail::encoded_frame_length(packet)});
    }

    task_returning_expected_void_t tunnelling_client::send_on_stream(const cspan_uint8_t cemi_bytes) noexcept(false)
    {
        if (cemi_bytes.size() > detail::max_cemi_size)
            co_return std::unexpected(make_error_code(error::payload_too_large));

        // Sends take turns rather than being refused, since none waits for an answer; one at a time also puts
        // their sequence numbers on the wire in order.
        const auto turn = co_await send_mutex_.lock();
        std::array<std::uint8_t, frame::max_datagram_size> packet {};
        if (const auto sequence = prepare_request(packet, cemi_bytes); !sequence.has_value())
            co_return std::unexpected(sequence.error());

        co_return co_await send_packet({packet.data(), detail::encoded_frame_length(packet)}, endpoint_kind::control);
    }

    task_returning_expected_void_t tunnelling_client::heartbeat() noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested())
            co_return std::unexpected(make_error_code(error::shutdown));
        if (transport_.stream_oriented())
            co_return co_await heartbeat_on_stream();
        operation_guard operation {*this};
        if (!operation.acquired())
            co_return std::unexpected(make_error_code(error::send_queue_full));

        std::array<std::uint8_t, frame::communication_header_size + connection::control_request_body_size> packet {};
        const auto prepared = with_session([&packet](const tunnelling_session& session) noexcept
                                           { return session.prepare_connectionstate_request_packet(packet); });
        if (!prepared.has_value())
            co_return std::unexpected(prepared.error());

        co_return co_await retry_heartbeat(packet);
    }

    task_returning_expected_void_t tunnelling_client::heartbeat_on_stream() noexcept(false)
    {
        const auto turn = co_await send_mutex_.lock();
        std::array<std::uint8_t, frame::communication_header_size + connection::control_request_body_size> packet {};
        const auto deadline = connectionstate_deadline_ms();
        const auto prepared = with_session(
            [&packet, deadline](tunnelling_session& session) noexcept -> expected_void_t
            {
                if (const auto encoded = session.prepare_connectionstate_request_packet(packet); !encoded.has_value())
                    return encoded;
                // The answer reaches whatever receive is running; poll() counts it missed if it never does.
                session.note_heartbeat_sent(deadline);
                return {};
            });
        if (!prepared.has_value())
            co_return prepared;

        co_return co_await send_packet(packet, endpoint_kind::control);
    }

    expected_void_t tunnelling_client::poll() noexcept
    {
        if (const auto secured = check_secure_session(); !secured.has_value())
            return secured;
        const auto now = now_ms();
        const auto result = with_session(
            [now](tunnelling_session& session) noexcept -> expected_void_t
            {
                if (const auto live = session.check_inactivity(now); !live.has_value())
                    return live;
                return session.check_heartbeat(now);
            });
        // A tunnel that poll closed has no further use for its connection.
        if (!result.has_value() && closed())
            transport_.close();
        return result;
    }

    datagram_task_t tunnelling_client::receive_datagram() noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested())
            co_return std::unexpected(make_error_code(error::shutdown));
        operation_guard operation {*this};
        if (!operation.acquired())
            co_return std::unexpected(make_error_code(error::send_queue_full));

        co_return co_await receive_datagram_impl();
    }

    std::error_code tunnelling_client::on_receive_failure(const std::error_code failure) noexcept
    {
        // A deadline, and an unwrapped frame a secure session refused, concern one receive and not the connection.
        if (!transport_.stream_oriented() || (failure == make_error_code(error::timeout)) ||
            (failure == make_error_code(error::secure_frame_required)))
            return failure;

        // A stream that failed carries nothing more, so the tunnel over it is gone and so is the connection - unless
        // the session was reset while this receive waited, in which case the reset already closed both.
        const auto reset = with_session(
            [](tunnelling_session& session) noexcept
            {
                const auto idle = session.state() == session_state::idle;
                if (!idle)
                    session.shutdown();
                return idle;
            });
        if (!reset)
            transport_.close();
        return failure;
    }

    datagram_task_t tunnelling_client::receive_datagram_impl() noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested())
            co_return std::unexpected(make_error_code(error::shutdown));

        if (const auto live = check_session_live(); !live.has_value())
            co_return std::unexpected(live.error());

        transport_peer peer {};
        auto* bytes = reinterpret_cast<std::byte*>(receive_buffer_.data());
        const auto received = co_await transport_.receive(span_byte_t {bytes, receive_buffer_.size()}, peer);
        if (!received)
            co_return std::unexpected(on_receive_failure(received.error()));
        if ((received.value() == 0u) || (received.value() > receive_buffer_.size()))
            co_return std::unexpected(make_error_code(error::invalid_length));

        const auto decoded = decode_datagram({receive_buffer_.data(), received.value()});
        if (!decoded.has_value())
            co_return std::unexpected(decoded.error());
        if (!peer_matches(peer, endpoint_for(decoded->service_type)))
            co_return std::unexpected(make_error_code(error::connection_failed));

        with_session([now = now_ms()](tunnelling_session& session) noexcept { session.observe_activity(now); });
        co_return decoded.value();
    }

    cemi_bytes_task_t tunnelling_client::receive_cemi() noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested())
            co_return std::unexpected(make_error_code(error::shutdown));
        operation_guard operation {*this};
        if (!operation.acquired())
            co_return std::unexpected(make_error_code(error::send_queue_full));

        auto received = co_await receive_cemi_impl();
        if (!received.has_value())
            co_return std::unexpected(received.error());
        co_return std::move(received->bytes);
    }

    tunnelling_client::received_cemi_task_t tunnelling_client::receive_cemi_impl() noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested() || (state() == session_state::closed))
            co_return std::unexpected(make_error_code(error::shutdown));

        for (;;)
        {
            const auto received = co_await receive_datagram_impl();
            if (!received.has_value())
                co_return std::unexpected(received.error());

            const auto* request = std::get_if<tunnelling_request_frame>(&received->payload);
            if (request == nullptr)
            {
                // Anything that is not a tunnelled frame is session bookkeeping: handled, then waited past.
                if (const auto handled = absorb_session_datagram(received.value()); !handled.has_value())
                    co_return std::unexpected(handled.error());
                continue;
            }

            const auto outcome = accept_indication(received.value(), *request);
            if (!outcome.accepted.has_value() && !outcome.duplicate)
                co_return std::unexpected(outcome.accepted.error());

            if (const auto sent = co_await acknowledge(*request); !sent)
                co_return std::unexpected(sent.error());
            if (outcome.duplicate)
                continue;
            if (auto delivered = deliver(*request); delivered.has_value())
                co_return std::move(*delivered);
        }
    }

    std::optional<tunnelling_client::received_cemi> tunnelling_client::deliver(const tunnelling_request_frame& request) const
        noexcept(false)
    {
        auto* const context = data_secure_.load();
        if (context == nullptr)
            return received_cemi {.frame = request.cemi, .bytes = byte_buffer_t(request.cemi_bytes.begin(), request.cemi_bytes.end())};
        // A telegram Data Secure refuses has been counted there. It was acknowledged already, and is read past.
        auto opened = context->open_frame(request.cemi_bytes.span());
        if (!opened.has_value())
            return std::nullopt;
        const auto frame = cemi::decode(*opened);
        if (!frame.has_value())
            return std::nullopt;
        return received_cemi {.frame = *frame, .bytes = std::move(*opened)};
    }

    task_returning_expected_void_t tunnelling_client::send_group_service(const group_address destination, const apci service,
                                                                         const apdu_payload& value,
                                                                         const l_data_options& options) noexcept(false)
    {
        auto* const context = data_secure_.load();
        // Data Secure binds the source address, so a telegram it secures names the tunnel's own address instead of leaving
        // the interface to fill it in.
        const auto source = (context != nullptr) ? assigned_address() : individual_address {};
        std::array<std::uint8_t, cemi::max_l_data_size> message {};
        const auto size = cemi::encode(
            message, {.source = source, .destination = destination.value(), .service = service, .payload = value, .options = options});
        if (!size.has_value())
            co_return std::unexpected(make_error_code(size.error()));
        if (context == nullptr)
            co_return co_await send(cspan_uint8_t {message.data(), *size});

        const auto secured = context->secure_frame({message.data(), *size});
        if (!secured.has_value())
            co_return std::unexpected(secured.error());
        co_return co_await send(*secured);
    }

    task_returning_expected_void_t tunnelling_client::write_group_value(const group_address destination, const dpt::payload& value,
                                                                        const l_data_options options) noexcept(false)
    {
        co_return co_await send_group_service(destination, apci::group_value_write, value.apdu(), options);
    }

    task_returning_expected_void_t tunnelling_client::read_group_value(const group_address destination,
                                                                       const l_data_options options) noexcept(false)
    {
        co_return co_await send_group_service(destination, apci::group_value_read, apdu_payload {}, options);
    }

    task_returning_expected_void_t tunnelling_client::respond_group_value(const group_address destination, const dpt::payload& value,
                                                                          const l_data_options options) noexcept(false)
    {
        co_return co_await send_group_service(destination, apci::group_value_response, value.apdu(), options);
    }

    telegram_task_t tunnelling_client::receive_telegram() noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested())
            co_return std::unexpected(make_error_code(error::shutdown));
        operation_guard operation {*this};
        if (!operation.acquired())
            co_return std::unexpected(make_error_code(error::send_queue_full));

        auto received = co_await receive_cemi_impl();
        if (!received.has_value())
            co_return std::unexpected(received.error());

        co_return telegram {.frame = received->frame, .bytes = std::move(received->bytes)};
    }

    task_returning_expected_void_t tunnelling_client::disconnect() noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested())
            co_return std::unexpected(make_error_code(error::shutdown));
        operation_guard operation {*this};
        if (!operation.acquired())
            co_return std::unexpected(make_error_code(error::send_queue_full));

        std::array<std::uint8_t, frame::communication_header_size + connection::control_request_body_size> packet {};
        const auto prepared =
            with_session([&packet](tunnelling_session& session) noexcept { return session.prepare_disconnect_request_packet(packet); });
        if (!prepared.has_value())
            co_return std::unexpected(prepared.error());

        auto outcome = co_await await_disconnect_response(packet);
        // A secure session ends with a SESSION_STATUS close. The connection served the tunnel alone, so it goes with it;
        // a datagram transport ignores this.
        if (secure_ != nullptr)
            static_cast<void>(co_await secure_->end_session());
        transport_.close();
        co_return outcome;
    }

    void tunnelling_client::shutdown() noexcept
    {
        with_session([](tunnelling_session& session) noexcept { session.shutdown(); });
        clear_data_peer();
        transport_.close();
    }

    void tunnelling_client::reset() noexcept
    {
        with_session([](tunnelling_session& session) noexcept { session.reset(); });
        clear_data_peer();
        // A stream tunnel reconnects over a new connection, never over the one its last session ran on.
        transport_.close();
    }

    void tunnelling_client::clear_data_peer() noexcept
    {
        data_peer_ = {};
        data_peer_length_ = 0u;
        data_peer_valid_ = false;
    }

    expected_void_t tunnelling_client::check_secure_session() noexcept
    {
        if (secure_ == nullptr)
            return {};
        const auto live = secure_->check_timeout();
        // A session that timed out takes the tunnel inside it along; its connection is closed already.
        if (!live.has_value())
            with_session([](tunnelling_session& session) noexcept { session.shutdown(); });
        return live;
    }

    bool tunnelling_client::keep_alive_due() const noexcept
    {
        return (secure_ != nullptr) && secure_->keep_alive_due();
    }

    task_returning_expected_void_t tunnelling_client::keep_alive() noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested())
            co_return std::unexpected(make_error_code(error::shutdown));
        if (secure_ == nullptr)
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        co_return co_await secure_->keep_alive();
    }

    secure::statistics tunnelling_client::secure_counters() const noexcept
    {
        return (secure_ != nullptr) ? secure_->counters() : secure::statistics {};
    }
}
