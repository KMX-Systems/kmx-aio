/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/client.hpp>

#include <chrono>
#include <cstring>
#include <netinet/in.h>

namespace kmx::aio::knx
{
    namespace detail
    {
        /// @brief Largest cEMI message that still fits one buffered datagram.
        constexpr std::size_t max_cemi_size =
            frame::max_datagram_size - frame::communication_header_size - frame::tunnelling_request_header_size;
    }

    tunnelling_client::operation_guard::operation_guard(tunnelling_client& owner) noexcept:
        owner_(&owner)
    {
        bool expected = false;
        acquired_ = owner.operation_active_.compare_exchange_strong(
            expected, true, std::memory_order_acquire, std::memory_order_relaxed);
    }

    tunnelling_client::operation_guard::~operation_guard() noexcept
    {
        if (acquired_)
            owner_->operation_active_.store(false, std::memory_order_release);
    }

    tunnelling_client::tunnelling_client(datagram_transport& transport,
                                         const sockaddr_storage& peer,
                                         const ::socklen_t peer_length,
                                         const tunnelling_config config,
                                         const clock_now_function clock_now,
                                         const secure::configuration secure_config,
                                         secure::provider* const secure_provider) noexcept:
        transport_(transport), peer_(peer), peer_length_(peer_length), clock_now_(clock_now),
        secure_config_(secure_config), secure_provider_(secure_provider),
        secure_replay_(secure_config_.replay_window), session_(config)
    {
        configured_peer_valid_ = (peer_length_ <= sizeof(sockaddr_storage));
        if (configured_peer_valid_ && (peer_.ss_family == AF_INET))
            configured_peer_valid_ = peer_length_ >= sizeof(sockaddr_in);
        else if (configured_peer_valid_ && (peer_.ss_family == AF_INET6))
            configured_peer_valid_ = peer_length_ >= sizeof(sockaddr_in6);
    }

    bool tunnelling_client::peer_matches(const transport_peer& peer, const endpoint_kind kind) const noexcept
    {
        const auto& expected_peer = (kind == endpoint_kind::data) ? data_peer_ : peer_;
        const auto expected_length = (kind == endpoint_kind::data) ? data_peer_length_ : peer_length_;
        if ((kind == endpoint_kind::data) && !data_peer_valid_)
            return false;

        if ((expected_length == 0u) || (expected_length > sizeof(sockaddr_storage)) ||
            (peer.length == 0u) || (peer.length > sizeof(sockaddr_storage)) ||
            ((expected_peer.ss_family != AF_UNSPEC) && (peer.address.ss_family != expected_peer.ss_family)))
            return false;
        if ((kind == endpoint_kind::control) && (expected_peer.ss_family == AF_UNSPEC))
            return (peer.address.ss_family == AF_UNSPEC) ||
                   ((peer.address.ss_family == AF_INET) && (peer.length >= sizeof(sockaddr_in)));

        switch (expected_peer.ss_family)
        {
            case AF_INET:
            {
                if ((expected_length < sizeof(sockaddr_in)) || (peer.length < sizeof(sockaddr_in)))
                    return false;
                const auto& expected = reinterpret_cast<const sockaddr_in&>(expected_peer);
                const auto& actual = reinterpret_cast<const sockaddr_in&>(peer.address);
                return (expected.sin_port == actual.sin_port) &&
                       (expected.sin_addr.s_addr == actual.sin_addr.s_addr);
            }
            case AF_INET6:
            {
                if ((expected_length < sizeof(sockaddr_in6)) || (peer.length < sizeof(sockaddr_in6)))
                    return false;
                const auto& expected = reinterpret_cast<const sockaddr_in6&>(expected_peer);
                const auto& actual = reinterpret_cast<const sockaddr_in6&>(peer.address);
                return (expected.sin6_port == actual.sin6_port) &&
                       (expected.sin6_flowinfo == actual.sin6_flowinfo) &&
                       (expected.sin6_scope_id == actual.sin6_scope_id) &&
                       (std::memcmp(&expected.sin6_addr, &actual.sin6_addr, sizeof(expected.sin6_addr)) == 0);
            }
            default:
                return (peer.length == expected_length) &&
                       (std::memcmp(&peer.address, &expected_peer, expected_length) == 0);
        }
    }

    std::uint32_t tunnelling_client::now_ms() const noexcept
    {
        if (clock_now_ != nullptr)
            return clock_now_();

        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    std::uint32_t tunnelling_client::operation_deadline_ms() const noexcept
    {
        return now_ms() + session_.ack_timeout_ms();
    }

    std::expected<std::vector<std::uint8_t>, std::error_code> tunnelling_client::protect_payload(
        const std::span<const std::uint8_t> payload, const std::uint64_t sequence) const noexcept
    {
        if (secure_config_.selected == secure::profile::none)
            return std::vector<std::uint8_t>(payload.begin(), payload.end());
        if (secure_provider_ == nullptr)
            return std::unexpected(make_error_code(error::secure_unsupported));
        return secure_provider_->protect(payload, sequence);
    }

    std::expected<std::vector<std::uint8_t>, std::error_code> tunnelling_client::unprotect_payload(
        const std::span<const std::uint8_t> payload, const std::uint64_t sequence) const noexcept
    {
        if (secure_config_.selected == secure::profile::none)
            return std::vector<std::uint8_t>(payload.begin(), payload.end());
        if (secure_provider_ == nullptr)
            return std::unexpected(make_error_code(error::secure_unsupported));
        return secure_provider_->unprotect(payload, sequence);
    }

    task_returning_expected_void_t tunnelling_client::send_packet(
        const cspan_uint8_t packet, const endpoint_kind kind) noexcept(false)
    {
        const auto& destination = (kind == endpoint_kind::data) ? data_peer_ : peer_;
        const auto destination_length = (kind == endpoint_kind::data) ? data_peer_length_ : peer_length_;
        if (!configured_peer_valid_ || (destination_length == 0u) ||
            (destination_length > sizeof(sockaddr_storage)) ||
            ((kind == endpoint_kind::data) && !data_peer_valid_))
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        const auto* bytes = reinterpret_cast<const std::byte*>(packet.data());
        const auto result = co_await transport_.send(cspan_byte_t { bytes, packet.size() },
                                                     reinterpret_cast<const sockaddr*>(&destination),
                                                     destination_length);
        if (!result)
            co_return std::unexpected(result.error());
        if (result.value() != packet.size())
            co_return std::unexpected(make_error_code(error::connection_failed));
        co_return expected_void_t {};
    }

    std::expected<std::vector<std::uint8_t>, std::error_code> tunnelling_client::secure_wrap_data_packet(
        const cspan_uint8_t packet, const std::uint64_t sequence) const noexcept
    {
        if (!secure_data_enabled())
            return std::vector<std::uint8_t>(packet.begin(), packet.end());
        if (secure_provider_ == nullptr)
            return std::unexpected(make_error_code(error::secure_unsupported));
        return secure::protect_packet(*secure_provider_, secure_config_.selected, packet, sequence);
    }

    std::expected<datagram, std::error_code> tunnelling_client::decode_received_packet(
        const cspan_uint8_t packet, const endpoint_kind kind) noexcept
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

        const auto unprotected = secure::unprotect_packet(
            *secure_provider_, secure_config_.selected, packet, &secure_replay_);
        if (!unprotected.has_value())
            return std::unexpected(unprotected.error());

        return decode_datagram(*unprotected);
    }

    task_returning_expected_void_t tunnelling_client::receive_into_session(const endpoint_kind kind) noexcept(false)
    {
        transport_peer peer {};
        auto* bytes = reinterpret_cast<std::byte*>(receive_buffer_.data());
        const auto result = co_await transport_.receive_until(
            span_byte_t { bytes, receive_buffer_.size() }, peer, session_.deadline_ms());
        if (!result)
            co_return std::unexpected(result.error());
        if (!peer_matches(peer, kind))
            co_return std::unexpected(make_error_code(error::connection_failed));
        if (result.value() > receive_buffer_.size())
            co_return std::unexpected(make_error_code(error::invalid_length));

        const auto decoded = decode_received_packet({ receive_buffer_.data(), result.value() }, kind);
        if (!decoded.has_value())
            co_return std::unexpected(decoded.error());

        if (const auto* response = std::get_if<connect_response_frame>(&decoded->payload))
        {
            const auto& endpoint = response->data_endpoint;
            if (endpoint.protocol != 0x01u || endpoint.endpoint.port == 0u)
                co_return std::unexpected(make_error_code(error::unsupported_hpai));

            data_peer_ = {};
            auto& data_address = reinterpret_cast<sockaddr_in&>(data_peer_);
            data_address.sin_family = AF_INET;
            data_address.sin_port = htons(endpoint.endpoint.port);
            std::memcpy(&data_address.sin_addr.s_addr, endpoint.endpoint.address.data(),
                        endpoint.endpoint.address.size());
            data_peer_length_ = sizeof(sockaddr_in);
            data_peer_valid_ = true;
        }
        else if (const auto* response = std::get_if<ipv6_connect_response_frame>(&decoded->payload))
        {
            const auto& endpoint = response->data_endpoint;
            if (endpoint.protocol != 0x01u || endpoint.endpoint.port == 0u)
                co_return std::unexpected(make_error_code(error::unsupported_hpai));

            data_peer_ = {};
            auto& data_address = reinterpret_cast<sockaddr_in6&>(data_peer_);
            data_address.sin6_family = AF_INET6;
            data_address.sin6_port = htons(endpoint.endpoint.port);
            std::memcpy(&data_address.sin6_addr, endpoint.endpoint.address.data(),
                        endpoint.endpoint.address.size());
            data_peer_length_ = sizeof(sockaddr_in6);
            data_peer_valid_ = true;
        }

        co_return session_.dispatch_session_datagram(decoded.value(), now_ms());
    }

    task_returning_expected_void_t tunnelling_client::connect(
        const connect_request_frame& request) noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested())
            co_return std::unexpected(make_error_code(error::shutdown));
        operation_guard operation {*this};
        if (!operation.acquired())
            co_return std::unexpected(make_error_code(error::send_queue_full));
        if (!secure::validate(secure_config_).has_value())
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        if ((secure_config_.selected != secure::profile::none) && (secure_provider_ == nullptr))
            co_return std::unexpected(make_error_code(error::secure_unsupported));
        if ((request.control_endpoint.protocol != 0x01u) ||
            (request.data_endpoint.protocol != 0x01u) ||
            (request.control_endpoint.endpoint.port == 0u) ||
            (request.data_endpoint.endpoint.port == 0u))
            co_return std::unexpected(make_error_code(
                (request.control_endpoint.protocol != 0x01u) || (request.data_endpoint.protocol != 0x01u)
                    ? error::unsupported_hpai
                    : error::invalid_configuration));

        clear_data_peer();
    reset_secure_state();
        std::array<std::uint8_t, 26u> packet {};
        if (const auto result = session_.start_connect(packet, request, operation_deadline_ms()); !result.has_value())
            co_return std::unexpected(result.error());

        for (;;)
        {
            if (stop_token.stop_requested())
                co_return std::unexpected(make_error_code(error::shutdown));
            if (const auto sent = co_await send_packet(session_.active_connect_packet(), endpoint_kind::control); !sent)
                co_return sent;

            const auto received = co_await receive_into_session(endpoint_kind::control);
            if (received.has_value())
                co_return received;
            if (received.error() != make_error_code(error::timeout))
                co_return std::unexpected(received.error());

            if (const auto retry = session_.on_connect_timeout(); !retry.has_value())
                co_return std::unexpected(retry.error());
        }
    }

    task_returning_expected_void_t tunnelling_client::connect(
        const ipv6_connect_request_frame& request) noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested())
            co_return std::unexpected(make_error_code(error::shutdown));
        operation_guard operation {*this};
        if (!operation.acquired())
            co_return std::unexpected(make_error_code(error::send_queue_full));
        if (!secure::validate(secure_config_).has_value())
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        if ((secure_config_.selected != secure::profile::none) && (secure_provider_ == nullptr))
            co_return std::unexpected(make_error_code(error::secure_unsupported));
        if ((request.control_endpoint.protocol != 0x01u) || (request.data_endpoint.protocol != 0x01u) ||
            (request.control_endpoint.endpoint.port == 0u) || (request.data_endpoint.endpoint.port == 0u))
            co_return std::unexpected(make_error_code(
                (request.control_endpoint.protocol != 0x01u) || (request.data_endpoint.protocol != 0x01u)
                    ? error::unsupported_hpai : error::invalid_configuration));

        clear_data_peer();
        reset_secure_state();
        std::array<std::uint8_t, 50u> packet {};
        if (const auto result = connection::encode_ipv6_connect_request_packet(packet, request); !result.has_value())
            co_return std::unexpected(result.error());
        if (const auto result = session_.start_connect_raw(packet, operation_deadline_ms()); !result.has_value())
            co_return std::unexpected(result.error());

        for (;;)
        {
            if (stop_token.stop_requested())
                co_return std::unexpected(make_error_code(error::shutdown));
            if (const auto sent = co_await send_packet({packet.data(), 50u}, endpoint_kind::control); !sent)
                co_return sent;
            const auto received = co_await receive_into_session(endpoint_kind::control);
            if (received.has_value())
                co_return received;
            if (received.error() != make_error_code(error::timeout))
                co_return std::unexpected(received.error());
            if (const auto retry = session_.on_connect_timeout(); !retry.has_value())
                co_return std::unexpected(retry.error());
        }
    }

    task_returning_expected_void_t tunnelling_client::send(
        const std::span<const std::uint8_t> cemi_bytes) noexcept(false)
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

        if (const auto sequence = session_.prepare_request_packet(packet,
                                                                  static_cast<std::uint8_t>(session_.channel_id()),
                                                                  cemi_bytes,
                                                                  operation_deadline_ms());
            !sequence.has_value())
        {
            co_return std::unexpected(sequence.error());
        }

        cspan_uint8_t outbound_packet = session_.active_request_packet();
        std::vector<std::uint8_t> secure_packet_storage {};
        if (secure_data_enabled())
        {
            const auto secure_packet = secure_wrap_data_packet(session_.active_request_packet(), next_secure_sequence());
            if (!secure_packet.has_value())
                co_return std::unexpected(secure_packet.error());
            secure_packet_storage = *secure_packet;
            outbound_packet = {secure_packet_storage.data(), secure_packet_storage.size()};
        }

        for (;;)
        {
            if (stop_token.stop_requested())
                co_return std::unexpected(make_error_code(error::shutdown));
            if (const auto sent = co_await send_packet(outbound_packet, endpoint_kind::data); !sent)
                co_return sent;

            const auto received = co_await receive_into_session(endpoint_kind::data);
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

        for (;;)
        {
            if (stop_token.stop_requested())
                co_return std::unexpected(make_error_code(error::shutdown));
            if (const auto sent = co_await send_packet(packet, endpoint_kind::control); !sent)
                co_return sent;

            const auto received = co_await receive_into_session(endpoint_kind::control);
            if (received.has_value())
                co_return received;
            if (received.error() == make_error_code(error::sequence_error))
                continue;
            if (received.error() != make_error_code(error::timeout))
                co_return std::unexpected(received.error());
            if (const auto timeout = session_.on_connectionstate_timeout(); !timeout.has_value())
            {
                if (timeout.error() == make_error_code(error::connection_failed))
                    continue;
                co_return std::unexpected(timeout.error());
            }
        }
    }

    task<std::expected<datagram, std::error_code>> tunnelling_client::receive_datagram() noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested())
            co_return std::unexpected(make_error_code(error::shutdown));
        operation_guard operation {*this};
        if (!operation.acquired())
            co_return std::unexpected(make_error_code(error::send_queue_full));

        co_return co_await receive_datagram_impl();
    }

    task<std::expected<datagram, std::error_code>> tunnelling_client::receive_datagram_impl() noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested())
            co_return std::unexpected(make_error_code(error::shutdown));

        if (session_.state() == session_state::closed)
            co_return std::unexpected(make_error_code(error::shutdown));
        if (const auto active = session_.check_inactivity(now_ms()); !active.has_value())
            co_return std::unexpected(active.error());

        transport_peer peer {};
        auto* bytes = reinterpret_cast<std::byte*>(receive_buffer_.data());
        const auto received = co_await transport_.receive(
            span_byte_t { bytes, receive_buffer_.size() }, peer);
        if (!received)
            co_return std::unexpected(received.error());
        if ((received.value() == 0u) || (received.value() > receive_buffer_.size()))
            co_return std::unexpected(make_error_code(error::invalid_length));

        const auto outer = decode_datagram({ receive_buffer_.data(), received.value() });
        if (!outer.has_value())
            co_return std::unexpected(outer.error());

        const auto expects_data_peer =
            (outer->service_type == frame::tunnelling_request_service) ||
            (outer->service_type == frame::tunnelling_ack_service) ||
            (secure_data_enabled() && (outer->service_type == secure::secure_service));
        const auto expected_kind = expects_data_peer && data_peer_valid_
            ? endpoint_kind::data
            : endpoint_kind::control;
        if (!peer_matches(peer, expected_kind))
            co_return std::unexpected(make_error_code(error::connection_failed));

        const auto decoded = decode_received_packet(
            { receive_buffer_.data(), received.value() }, expected_kind);
        if (!decoded.has_value())
            co_return std::unexpected(decoded.error());

        session_.observe_activity(now_ms());
        co_return decoded.value();
    }

    task<std::expected<std::vector<std::uint8_t>, std::error_code>> tunnelling_client::receive_cemi() noexcept(false)
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

    task<std::expected<tunnelling_client::received_cemi, std::error_code>>
    tunnelling_client::receive_cemi_impl() noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (stop_token.stop_requested())
            co_return std::unexpected(make_error_code(error::shutdown));

        if (session_.state() == session_state::closed)
            co_return std::unexpected(make_error_code(error::shutdown));

        for (;;)
        {
            const auto received = co_await receive_datagram_impl();
            if (!received.has_value())
                co_return std::unexpected(received.error());

            if (std::holds_alternative<connectionstate_response_frame>(received->payload))
            {
                const auto processed = session_.on_datagram(received.value());
                if (!processed.has_value())
                    co_return std::unexpected(processed.error());
                continue;
            }
            if (std::holds_alternative<tunnelling_ack_frame>(received->payload))
                continue;

            const auto* request = std::get_if<tunnelling_request_frame>(&received->payload);
            if (request == nullptr)
                co_return std::unexpected(make_error_code(error::unsupported_service));

            const auto accepted = session_.dispatch_session_datagram(received.value(), now_ms());
            if (!accepted.has_value())
            {
                if (accepted.error() == make_error_code(error::sequence_error) &&
                    session_.duplicate_indication(*request))
                {
                    std::array<std::uint8_t, frame::communication_header_size + frame::tunnelling_ack_size> duplicate_ack {};
                    const auto prepared = session_.prepare_tunnelling_ack_packet(duplicate_ack, *request);
                    if (!prepared.has_value())
                        co_return std::unexpected(prepared.error());
                    if (secure_data_enabled())
                    {
                        const auto secured = secure_wrap_data_packet(duplicate_ack, next_secure_sequence());
                        if (!secured.has_value())
                            co_return std::unexpected(secured.error());
                        if (const auto sent = co_await send_packet(
                                {secured->data(), secured->size()}, endpoint_kind::data); !sent)
                            co_return std::unexpected(sent.error());
                    }
                    else
                    {
                        if (const auto sent = co_await send_packet(duplicate_ack, endpoint_kind::data); !sent)
                            co_return std::unexpected(sent.error());
                    }
                    continue;
                }
                co_return std::unexpected(accepted.error());
            }

            std::array<std::uint8_t, frame::communication_header_size + frame::tunnelling_ack_size> ack {};
            const auto prepared = session_.prepare_tunnelling_ack_packet(ack, *request);
            if (!prepared.has_value())
                co_return std::unexpected(prepared.error());
            if (secure_data_enabled())
            {
                const auto secured = secure_wrap_data_packet(ack, next_secure_sequence());
                if (!secured.has_value())
                    co_return std::unexpected(secured.error());
                if (const auto sent = co_await send_packet(
                        {secured->data(), secured->size()}, endpoint_kind::data); !sent)
                    co_return std::unexpected(sent.error());
            }
            else
            {
                if (const auto sent = co_await send_packet(ack, endpoint_kind::data); !sent)
                    co_return std::unexpected(sent.error());
            }

            co_return received_cemi {
                .frame = request->cemi,
                .bytes = std::vector<std::uint8_t>(request->cemi_bytes.begin(), request->cemi_bytes.end()),
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

    task<std::expected<telegram, std::error_code>> tunnelling_client::receive_telegram() noexcept(false)
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

        for (;;)
        {
            if (stop_token.stop_requested())
                co_return std::unexpected(make_error_code(error::shutdown));
            if (const auto sent = co_await send_packet(packet, endpoint_kind::control); !sent)
                co_return sent;

            transport_peer peer {};
            auto* bytes = reinterpret_cast<std::byte*>(receive_buffer_.data());
            const auto received = co_await transport_.receive_until(
                span_byte_t { bytes, receive_buffer_.size() }, peer, operation_deadline_ms());
            if (received)
            {
                if (!peer_matches(peer, endpoint_kind::control))
                    co_return std::unexpected(make_error_code(error::connection_failed));
                if ((received.value() == 0u) || (received.value() > receive_buffer_.size()))
                    co_return std::unexpected(make_error_code(error::invalid_length));
                const auto decoded = decode_datagram({ receive_buffer_.data(), received.value() });
                if (!decoded.has_value())
                    co_return std::unexpected(decoded.error());
                if (decoded->service_type != connection::disconnect_response_service)
                    continue;
                co_return session_.on_datagram(decoded.value());
            }
            if (received.error() != make_error_code(error::timeout))
                co_return std::unexpected(received.error());

            if (const auto retry = session_.on_disconnect_timeout(); !retry.has_value())
                co_return std::unexpected(retry.error());
        }
    }
}
