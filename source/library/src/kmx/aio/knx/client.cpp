/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/client.hpp>

#include <chrono>
#include <cstring>
#include <netinet/in.h>

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
        static_assert(max_cemi_size <= (frame::max_datagram_size - frame::communication_header_size -
                                        frame::tunnelling_request_header_size),
                      "a sendable cEMI message must still fit one buffered datagram");
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
                                         const tunnelling_config config, const clock_now_function clock_now,
                                         const secure::configuration secure_config, secure::provider* const secure_provider) noexcept:
        transport_(transport),
        peer_length_(peer_length),
        clock_now_(clock_now),
        secure_config_(secure_config),
        secure_provider_(secure_provider),
        secure_replay_(secure_config_.replay_window),
        session_(config)
    {
        // Only peer_length octets are read: peer may point at a sockaddr_in, which is an eighth the size of
        // the sockaddr_storage it is stored into.
        configured_peer_valid_ = store_socket_address(peer_, peer, peer_length_);
        if (configured_peer_valid_ && (peer_.ss_family == AF_INET))
            configured_peer_valid_ = peer_length_ >= sizeof(sockaddr_in);
        else if (configured_peer_valid_ && (peer_.ss_family == AF_INET6))
            configured_peer_valid_ = peer_length_ >= sizeof(sockaddr_in6);
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

    expected_byte_buffer_t tunnelling_client::protect_payload(const cspan_uint8_t payload, const std::uint64_t sequence) const noexcept
    {
        if (secure_config_.selected == secure::profile::none)
            return byte_buffer_t(payload.begin(), payload.end());
        if (secure_provider_ == nullptr)
            return std::unexpected(make_error_code(error::secure_unsupported));
        return secure_provider_->protect(payload, sequence);
    }

    expected_byte_buffer_t tunnelling_client::unprotect_payload(const cspan_uint8_t payload, const std::uint64_t sequence) const noexcept
    {
        if (secure_config_.selected == secure::profile::none)
            return byte_buffer_t(payload.begin(), payload.end());
        if (secure_provider_ == nullptr)
            return std::unexpected(make_error_code(error::secure_unsupported));
        return secure_provider_->unprotect(payload, sequence);
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

    expected_byte_buffer_t tunnelling_client::secure_wrap_data_packet(
        const cspan_uint8_t packet, const std::uint64_t sequence) const noexcept
    {
        if (!secure_data_enabled())
            return byte_buffer_t(packet.begin(), packet.end());
        if (secure_provider_ == nullptr)
            return std::unexpected(make_error_code(error::secure_unsupported));
        return secure::protect_packet(*secure_provider_, secure_config_.selected, packet, sequence);
    }

    datagram_result_t tunnelling_client::decode_received_packet(const cspan_uint8_t packet,
                                                                                       const endpoint_kind kind) noexcept
    {
        const auto decoded = decode_datagram(packet);
        if (!decoded.has_value())
            return std::unexpected(decoded.error());

        if ((kind != endpoint_kind::data) || !secure_data_enabled())
            return decoded;

        if (decoded->service_type != secure::secure_service)
            return std::unexpected(make_error_code(error::secure_unsupported));
        if (secure_provider_ == nullptr)
            return std::unexpected(make_error_code(error::secure_unsupported));

        const auto unprotected = secure::unprotect_packet(*secure_provider_, secure_config_.selected, packet, &secure_replay_);
        if (!unprotected.has_value())
            return std::unexpected(unprotected.error());

        return decode_datagram(*unprotected);
    }

    bool tunnelling_client::is_retransmission(const expected_void_t& accepted,
                                              const tunnelling_request_frame& request) const noexcept
    {
        // A repeat of the frame just seen: the server is retrying because it missed the acknowledgement,
        // not because it has anything new to deliver. It is acknowledged again and dropped.
        return !accepted.has_value() && (accepted.error() == make_error_code(error::sequence_error)) &&
               session_.duplicate_indication(request);
    }

    expected_void_t tunnelling_client::check_session_live() noexcept
    {
        if (session_.state() == session_state::closed)
            return std::unexpected(make_error_code(error::shutdown));
        return session_.check_inactivity(now_ms());
    }

    expected_void_t tunnelling_client::absorb_session_datagram(const datagram& value) noexcept
    {
        // A heartbeat answer advances the session; an acknowledgement for a frame this client already
        // stopped waiting on is simply dropped. Anything else is not ours to receive here.
        if (std::holds_alternative<connectionstate_response_frame>(value.payload))
            return session_.on_datagram(value);
        if (std::holds_alternative<tunnelling_ack_frame>(value.payload))
            return {};
        return std::unexpected(make_error_code(error::unsupported_service));
    }

    tunnelling_client::endpoint_kind tunnelling_client::endpoint_for(const std::uint16_t service_type) const noexcept
    {
        // Only the services that travel the data channel are expected from the data peer, and only once
        // one has been learned; everything else, this build's secure envelope included, is control traffic.
        const auto on_data_channel = (service_type == frame::tunnelling_request_service) ||
                                     (service_type == frame::tunnelling_ack_service) ||
                                     (secure_data_enabled() && (service_type == secure::secure_service));
        return (on_data_channel && data_peer_valid_) ? endpoint_kind::data : endpoint_kind::control;
    }

    task_returning_expected_void_t tunnelling_client::send_data_packet(const cspan_uint8_t packet) noexcept(false)
    {
        // A datagram leaves the data channel the way that channel is configured: wrapped when it is
        // secured, plain otherwise. Stated once here rather than at each of the sites that send one.
        if (!secure_data_enabled())
            co_return co_await send_packet(packet, endpoint_kind::data);

        const auto secured = secure_wrap_data_packet(packet, next_secure_sequence());
        if (!secured.has_value())
            co_return std::unexpected(secured.error());
        co_return co_await send_packet({secured->data(), secured->size()}, endpoint_kind::data);
    }

    task_returning_expected_void_t tunnelling_client::acknowledge(const tunnelling_request_frame& request) noexcept(false)
    {
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

            const auto received = co_await receive_into_session(endpoint_kind::data, session_.deadline_ms());
            if (received.has_value())
                co_return received;
            if (received.error() == make_error_code(error::sequence_error))
                continue;
            if (received.error() != make_error_code(error::timeout))
                co_return std::unexpected(received.error());

            if (const auto retry = session_.on_timeout(); !retry.has_value())
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
            if (const auto timeout = session_.on_connectionstate_timeout();
                !timeout.has_value() && (timeout.error() != make_error_code(error::connection_failed)))
                co_return std::unexpected(timeout.error());
        }
    }

    task_returning_expected_void_t tunnelling_client::await_disconnect_response(const cspan_uint8_t packet) noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        for (;;)
        {
            if (stop_token.stop_requested())
                co_return std::unexpected(make_error_code(error::shutdown));
            if (const auto sent = co_await send_packet(packet, endpoint_kind::control); !sent)
                co_return sent;

            transport_peer peer {};
            auto* bytes = reinterpret_cast<std::byte*>(receive_buffer_.data());
            const auto received =
                co_await transport_.receive_until(span_byte_t {bytes, receive_buffer_.size()}, peer, disconnect_deadline_ms());
            if (!received)
            {
                if (received.error() != make_error_code(error::timeout))
                    co_return std::unexpected(received.error());
                if (const auto retry = session_.on_disconnect_timeout(); !retry.has_value())
                    co_return std::unexpected(retry.error());
                continue;
            }

            if (const auto answer = on_disconnect_datagram(peer, received.value()); answer.has_value())
                co_return *answer;
        }
    }

    optional_expected_void_t tunnelling_client::on_disconnect_datagram(const transport_peer& peer,
                                                                       const std::size_t size) noexcept
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
        return session_.on_datagram(decoded.value());
    }

    expected_void_t tunnelling_client::validate_secure_ready() const noexcept
    {
        if (!secure::validate(secure_config_).has_value())
            return std::unexpected(make_error_code(error::invalid_configuration));
        if ((secure_config_.selected != secure::profile::none) && (secure_provider_ == nullptr))
            return std::unexpected(make_error_code(error::secure_unsupported));
        return {};
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

            const auto received = co_await receive_into_session(endpoint_kind::control, session_.deadline_ms());
            if (received.has_value())
                co_return received;
            if (received.error() != make_error_code(error::timeout))
                co_return std::unexpected(received.error());

            if (const auto retry = session_.on_connect_timeout(); !retry.has_value())
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

    expected_void_t tunnelling_client::adopt_data_endpoint(const datagram& value) noexcept
    {
        if (const auto* response = std::get_if<connect_response_frame>(&value.payload))
        {
            const auto& endpoint = response->data_endpoint;
            if (endpoint.protocol != 0x01u)
                return std::unexpected(make_error_code(error::unsupported_hpai));
            if (route_back(endpoint))
            {
                // The server named no data endpoint of its own, so its control endpoint stays in use.
                data_peer_ = peer_;
                data_peer_length_ = peer_length_;
                data_peer_valid_ = configured_peer_valid_;
                return {};
            }
            if (endpoint.endpoint.port == 0u)
                return std::unexpected(make_error_code(error::unsupported_hpai));

            adopt_data_peer(endpoint);
        }
        else if (const auto* response = std::get_if<ipv6_connect_response_frame>(&value.payload))
        {
            const auto& endpoint = response->data_endpoint;
            if ((endpoint.protocol != 0x01u) || (endpoint.endpoint.port == 0u))
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

        const auto decoded = decode_received_packet({receive_buffer_.data(), result.value()}, kind);
        if (!decoded.has_value())
            co_return std::unexpected(decoded.error());

        if (const auto adopted = adopt_data_endpoint(decoded.value()); !adopted.has_value())
            co_return std::unexpected(adopted.error());

        co_return session_.dispatch_session_datagram(decoded.value(), now_ms());
    }

    /// @brief Checks both endpoints of a connect request are usable.
    /// @tparam Endpoint The HPAI type, which differs between the IPv4 and IPv6 requests.
    /// @param control The control endpoint the request names.
    /// @param data The data endpoint the request names.
    /// @return Nothing, or why the pair cannot be connected with.
    /// @details An all-zero HPAI is the route-back form, not a misconfigured one: it asks the server to
    ///          answer the source of the datagram, which is the only way a client behind NAT is reachable.
    template <typename Endpoint>
    [[nodiscard]] static expected_void_t validate_connect_endpoints(const Endpoint& control, const Endpoint& data) noexcept
    {
        if ((control.protocol != 0x01u) || (data.protocol != 0x01u))
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
        if (const auto ready = validate_secure_ready(); !ready.has_value())
            co_return std::unexpected(ready.error());
        if (const auto valid = validate_connect_endpoints(request.control_endpoint, request.data_endpoint);
            !valid.has_value())
            co_return std::unexpected(valid.error());

        clear_data_peer();
        reset_secure_state();
        std::array<std::uint8_t, 26u> packet {};
        if (const auto result = session_.start_connect(packet, request, connect_deadline_ms()); !result.has_value())
            co_return std::unexpected(result.error());

        co_return co_await retry_connect(session_.active_connect_packet());
    }

    task_returning_expected_void_t tunnelling_client::connect(const ipv6_connect_request_frame& request) noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested())
            co_return std::unexpected(make_error_code(error::shutdown));
        operation_guard operation {*this};
        if (!operation.acquired())
            co_return std::unexpected(make_error_code(error::send_queue_full));
        if (const auto ready = validate_secure_ready(); !ready.has_value())
            co_return std::unexpected(ready.error());
        if (const auto valid = validate_connect_endpoints(request.control_endpoint, request.data_endpoint);
            !valid.has_value())
            co_return std::unexpected(valid.error());

        clear_data_peer();
        reset_secure_state();
        std::array<std::uint8_t, 50u> packet {};
        if (const auto result = connection::encode_ipv6_connect_request_packet(packet, request); !result.has_value())
            co_return std::unexpected(result.error());
        if (const auto result = session_.start_connect_raw(packet, connect_deadline_ms()); !result.has_value())
            co_return std::unexpected(result.error());

        co_return co_await retry_connect({packet.data(), 50u});
    }

    task_returning_expected_void_t tunnelling_client::send(const cspan_uint8_t cemi_bytes) noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested())
            co_return std::unexpected(make_error_code(error::shutdown));
        operation_guard operation {*this};
        if (!operation.acquired())
            co_return std::unexpected(make_error_code(error::send_queue_full));

        if (cemi_bytes.size() > detail::max_cemi_size)
            co_return std::unexpected(make_error_code(error::payload_too_large));

        std::array<std::uint8_t, frame::max_datagram_size> packet {};

        if (const auto sequence = session_.prepare_request_packet(packet, static_cast<std::uint8_t>(session_.channel_id()), cemi_bytes,
                                                                  operation_deadline_ms());
            !sequence.has_value())
            co_return std::unexpected(sequence.error());

        cspan_uint8_t outbound_packet = session_.active_request_packet();
        byte_buffer_t secure_packet_storage {};
        if (secure_data_enabled())
        {
            const auto secure_packet = secure_wrap_data_packet(session_.active_request_packet(), next_secure_sequence());
            if (!secure_packet.has_value())
                co_return std::unexpected(secure_packet.error());
            secure_packet_storage = *secure_packet;
            outbound_packet = {secure_packet_storage.data(), secure_packet_storage.size()};
        }

        co_return co_await retry_request(outbound_packet);
    }

    task_returning_expected_void_t tunnelling_client::heartbeat() noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested())
            co_return std::unexpected(make_error_code(error::shutdown));
        operation_guard operation {*this};
        if (!operation.acquired())
            co_return std::unexpected(make_error_code(error::send_queue_full));

        std::array<std::uint8_t, 8u> packet {};
        if (const auto prepared = session_.prepare_connectionstate_request_packet(packet); !prepared.has_value())
            co_return std::unexpected(prepared.error());

        co_return co_await retry_heartbeat(packet);
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
            co_return std::unexpected(received.error());
        if ((received.value() == 0u) || (received.value() > receive_buffer_.size()))
            co_return std::unexpected(make_error_code(error::invalid_length));

        const auto outer = decode_datagram({receive_buffer_.data(), received.value()});
        if (!outer.has_value())
            co_return std::unexpected(outer.error());

        const auto expected_kind = endpoint_for(outer->service_type);
        if (!peer_matches(peer, expected_kind))
            co_return std::unexpected(make_error_code(error::connection_failed));

        const auto decoded = decode_received_packet({receive_buffer_.data(), received.value()}, expected_kind);
        if (!decoded.has_value())
            co_return std::unexpected(decoded.error());

        session_.observe_activity(now_ms());
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
        if (stop_token.stop_requested() || (session_.state() == session_state::closed))
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

            const auto accepted = session_.dispatch_session_datagram(received.value(), now_ms());
            const auto duplicate = is_retransmission(accepted, *request);
            if (!accepted.has_value() && !duplicate)
                co_return std::unexpected(accepted.error());

            if (const auto sent = co_await acknowledge(*request); !sent)
                co_return std::unexpected(sent.error());
            if (duplicate)
                continue;

            co_return received_cemi {
                .frame = request->cemi,
                .bytes = byte_buffer_t(request->cemi_bytes.begin(), request->cemi_bytes.end()),
            };
        }
    }

    task_returning_expected_void_t tunnelling_client::send_group_service(const group_address destination, const apci service,
                                                                         const apdu_payload& value,
                                                                         const l_data_options& options) noexcept(false)
    {
        std::array<std::uint8_t, cemi::max_l_data_size> message {};
        const auto size = cemi::encode(message, cemi_message_code::l_data_req, individual_address {}, destination, service, value, options);
        if (!size.has_value())
            co_return std::unexpected(make_error_code(size.error()));

        co_return co_await send(cspan_uint8_t {message.data(), *size});
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

        std::array<std::uint8_t, 8u> packet {};
        if (const auto result = session_.prepare_disconnect_request_packet(packet); !result.has_value())
            co_return std::unexpected(result.error());

        co_return co_await await_disconnect_response(packet);
    }

    void tunnelling_client::shutdown() noexcept
    {
        session_.shutdown();
        clear_data_peer();
        reset_secure_state();
    }

    void tunnelling_client::reset() noexcept
    {
        session_.reset();
        clear_data_peer();
        reset_secure_state();
    }

    void tunnelling_client::clear_data_peer() noexcept
    {
        data_peer_ = {};
        data_peer_length_ = 0u;
        data_peer_valid_ = false;
    }
}
