/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/server.hpp>

#include <chrono>
#include <cstring>

namespace kmx::aio::knx
{
    namespace
    {
        [[nodiscard]] std::expected<void, std::error_code> validate_connect_request(
            const hpai& control, const hpai& data) noexcept
        {
            if ((control.protocol != 0x01u) || (data.protocol != 0x01u))
                return std::unexpected(make_error_code(error::unsupported_hpai));
            if ((control.endpoint.port == 0u) || (data.endpoint.port == 0u))
                return std::unexpected(make_error_code(error::invalid_configuration));
            return {};
        }

        [[nodiscard]] std::expected<void, std::error_code> validate_connect_request(
            const ipv6_hpai& control, const ipv6_hpai& data) noexcept
        {
            if ((control.protocol != 0x01u) || (data.protocol != 0x01u))
                return std::unexpected(make_error_code(error::unsupported_hpai));
            if ((control.endpoint.port == 0u) || (data.endpoint.port == 0u))
                return std::unexpected(make_error_code(error::invalid_configuration));
            return {};
        }

        [[nodiscard]] transport_peer make_data_peer(
            const transport_peer& control_peer, const hpai& endpoint) noexcept
        {
            auto result = control_peer;
            if (result.address.ss_family == AF_INET)
            {
                auto& address = reinterpret_cast<sockaddr_in&>(result.address);
                std::memcpy(&address.sin_addr.s_addr, endpoint.endpoint.address.data(),
                            endpoint.endpoint.address.size());
                address.sin_port = htons(endpoint.endpoint.port);
                result.length = sizeof(sockaddr_in);
            }
            return result;
        }

        [[nodiscard]] transport_peer make_data_peer(
            const transport_peer& control_peer, const ipv6_hpai& endpoint) noexcept
        {
            auto result = control_peer;
            if (result.address.ss_family == AF_INET6)
            {
                auto& address = reinterpret_cast<sockaddr_in6&>(result.address);
                std::memcpy(&address.sin6_addr, endpoint.endpoint.address.data(),
                            endpoint.endpoint.address.size());
                address.sin6_port = htons(endpoint.endpoint.port);
                result.length = sizeof(sockaddr_in6);
            }
            return result;
        }
    }

    generic_server::generic_server(datagram_transport& transport, const server_config config,
                                   const server_clock_now_function clock_now) noexcept:
        transport_(transport), config_(config), clock_now_(clock_now)
    {
        if (config_.max_channels == 0u)
            config_.max_channels = 1u;
    }

    std::uint32_t generic_server::now_ms() const noexcept
    {
        if (clock_now_ != nullptr)
            return clock_now_();
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    void generic_server::observe_activity(channel& value) noexcept
    {
        value.last_activity_ms = now_ms();
    }

    bool generic_server::channel_active(const std::uint8_t channel_id) const noexcept
    {
        return (channel_id != 0u) && channels_[channel_id].active;
    }

    std::uint8_t generic_server::active_channels() const noexcept
    {
        std::uint8_t count = 0u;
        for (std::uint16_t id = 1u; id < channels_.size(); ++id)
        {
            if (channels_[id].active)
                ++count;
        }
        return count;
    }

    std::expected<std::uint8_t, std::error_code> generic_server::allocate_channel() noexcept
    {
        const auto limit = std::min<std::uint16_t>(config_.max_channels, 255u);
        for (std::uint16_t id = 1u; id <= limit; ++id)
        {
            if (!channels_[id].active)
                return static_cast<std::uint8_t>(id);
        }
        return std::unexpected(make_error_code(error::send_queue_full));
    }

    individual_address generic_server::assigned_address(const std::uint8_t channel_id) const noexcept
    {
        const auto base = config_.first_assigned_address.value();
        return individual_address {static_cast<std::uint16_t>(base + channel_id - 1u)};
    }

    void generic_server::release_channel(const std::uint8_t channel_id) noexcept
    {
        if (channel_id != 0u)
            channels_[channel_id] = channel {};
    }

    bool generic_server::peer_matches(const channel& value, const transport_peer& peer,
                                      const bool data_endpoint) const noexcept
    {
        const auto& expected_peer = data_endpoint ? value.data_peer : value.peer;
        if (!value.active || (expected_peer.length == 0u) || (expected_peer.length != peer.length) ||
            (expected_peer.address.ss_family != peer.address.ss_family))
            return false;

        if (peer.address.ss_family == AF_INET && peer.length >= sizeof(sockaddr_in))
        {
            const auto& expected = reinterpret_cast<const sockaddr_in&>(expected_peer.address);
            const auto& actual = reinterpret_cast<const sockaddr_in&>(peer.address);
            return (expected.sin_port == actual.sin_port) && (expected.sin_addr.s_addr == actual.sin_addr.s_addr);
        }
        if (peer.address.ss_family == AF_INET6 && peer.length >= sizeof(sockaddr_in6))
        {
            const auto& expected = reinterpret_cast<const sockaddr_in6&>(expected_peer.address);
            const auto& actual = reinterpret_cast<const sockaddr_in6&>(peer.address);
            return (expected.sin6_port == actual.sin6_port) &&
                   (expected.sin6_scope_id == actual.sin6_scope_id) &&
                   (std::memcmp(&expected.sin6_addr, &actual.sin6_addr, sizeof(expected.sin6_addr)) == 0);
        }
        return std::memcmp(&expected_peer.address, &peer.address, peer.length) == 0;
    }

    task_returning_expected_void_t generic_server::send_datagram(
        const std::vector<std::uint8_t>& packet, const transport_peer& peer) noexcept(false)
    {
        const auto* bytes = reinterpret_cast<const std::byte*>(packet.data());
        const auto sent = co_await transport_.send(
            cspan_byte_t {bytes, packet.size()}, reinterpret_cast<const sockaddr*>(&peer.address), peer.length);
        if (!sent)
            co_return std::unexpected(sent.error());
        if (*sent != packet.size())
            co_return std::unexpected(make_error_code(error::connection_failed));
        co_return expected_void_t {};
    }

    task<std::expected<server_event, std::error_code>> generic_server::serve_once() noexcept(false)
    {
        if (shutdown_)
            co_return std::unexpected(make_error_code(error::shutdown));
        if (const auto active = poll(); !active.has_value())
            co_return std::unexpected(active.error());

        transport_peer peer {};
        auto* bytes = reinterpret_cast<std::byte*>(receive_buffer_.data());
        const auto received = co_await transport_.receive(
            span_byte_t {bytes, receive_buffer_.size()}, peer);
        if (!received)
            co_return std::unexpected(received.error());
        if ((received.value() == 0u) || (received.value() > receive_buffer_.size()))
            co_return std::unexpected(make_error_code(error::invalid_length));

        const auto decoded = decode_datagram({receive_buffer_.data(), received.value()});
        if (!decoded.has_value())
            co_return std::unexpected(decoded.error());

        std::vector<std::uint8_t> response;

        if (const auto* request = std::get_if<ipv6_connect_request_frame>(&decoded->payload))
        {
            if (decoded->service_type != connection::connect_request_service)
                co_return std::unexpected(make_error_code(error::invalid_configuration));
            if (const auto valid = validate_connect_request(request->control_endpoint, request->data_endpoint);
                !valid.has_value())
                co_return std::unexpected(valid.error());

            const auto channel_id = allocate_channel();
            const auto selected = channel_id.has_value() ? *channel_id : 0u;
            const auto status = channel_id.has_value() ? connect_status::no_error : connect_status::no_more_connections;
            const auto assigned = channel_id.has_value() ? assigned_address(selected) : individual_address {};

            if (channel_id.has_value())
            {
                channels_[selected].active = true;
                channels_[selected].peer = peer;
                channels_[selected].data_peer = make_data_peer(peer, request->data_endpoint);
                channels_[selected].assigned_address = assigned;
                channels_[selected].sequence = 0u;
                observe_activity(channels_[selected]);
            }

            response.resize(frame::communication_header_size + connection::ipv6_connect_response_body_size);
            const auto encoded = connection::encode_ipv6_connect_response_packet(
                response,
                ipv6_connect_response_frame {static_cast<std::uint8_t>(selected), status,
                                              request->data_endpoint, assigned});
            if (!encoded.has_value())
                co_return std::unexpected(encoded.error());
            const auto sent = co_await send_datagram(response, peer);
            if (!sent)
                co_return std::unexpected(sent.error());
            if (!channel_id.has_value())
                co_return std::unexpected(make_error_code(error::send_queue_full));
            co_return server_event {static_cast<std::uint8_t>(selected), {}};
        }

        if (const auto* request = std::get_if<connect_request_frame>(&decoded->payload))
        {
            if (decoded->service_type != connection::connect_request_service)
                co_return std::unexpected(make_error_code(error::invalid_configuration));
            if (const auto valid = validate_connect_request(request->control_endpoint, request->data_endpoint);
                !valid.has_value())
                co_return std::unexpected(valid.error());

            const auto channel_id = allocate_channel();
            const auto selected = channel_id.has_value() ? *channel_id : 0u;
            const auto status = channel_id.has_value() ? connect_status::no_error : connect_status::no_more_connections;
            const auto assigned = channel_id.has_value() ? assigned_address(selected) : individual_address {};
            const auto data_endpoint = request->data_endpoint;

            if (channel_id.has_value())
            {
                channels_[selected].active = true;
                channels_[selected].peer = peer;
                channels_[selected].data_peer = make_data_peer(peer, data_endpoint);
                channels_[selected].data_endpoint = data_endpoint;
                channels_[selected].assigned_address = assigned;
                channels_[selected].sequence = 0u;
                observe_activity(channels_[selected]);
            }

            response.resize(frame::communication_header_size + connection::connect_response_body_size);
            const auto encoded = connection::encode_connect_response_packet(
                response,
                connect_response_frame {static_cast<std::uint8_t>(selected), status, data_endpoint, assigned});
            if (!encoded.has_value())
                co_return std::unexpected(encoded.error());
            const auto sent = co_await send_datagram(response, peer);
            if (!sent)
                co_return std::unexpected(sent.error());
            if (!channel_id.has_value())
                co_return std::unexpected(make_error_code(error::send_queue_full));
            co_return server_event {static_cast<std::uint8_t>(selected), {}};
        }

        if (const auto* request = std::get_if<connectionstate_request_frame>(&decoded->payload))
        {
            if (!channel_active(request->channel_id) || !peer_matches(channels_[request->channel_id], peer))
                co_return std::unexpected(make_error_code(error::sequence_error));
            response.resize(frame::communication_header_size + 2u);
            const auto encoded = connection::encode_connectionstate_response_packet(
                response, connectionstate_response_frame {request->channel_id, connect_status::no_error});
            if (!encoded.has_value())
                co_return std::unexpected(encoded.error());
            const auto sent = co_await send_datagram(response, peer);
            if (!sent)
                co_return std::unexpected(sent.error());
            observe_activity(channels_[request->channel_id]);
            co_return server_event {request->channel_id, {}};
        }

        if (const auto* request = std::get_if<disconnect_request_frame>(&decoded->payload))
        {
            if (!channel_active(request->channel_id) || !peer_matches(channels_[request->channel_id], peer))
                co_return std::unexpected(make_error_code(error::sequence_error));
            response.resize(frame::communication_header_size + 2u);
            const auto encoded = connection::encode_disconnect_response_packet(
                response, disconnect_response_frame {request->channel_id, connect_status::no_error});
            if (!encoded.has_value())
                co_return std::unexpected(encoded.error());
            const auto sent = co_await send_datagram(response, peer);
            if (!sent)
                co_return std::unexpected(sent.error());
            observe_activity(channels_[request->channel_id]);
            release_channel(request->channel_id);
            co_return server_event {request->channel_id, {}};
        }

        if (const auto* request = std::get_if<tunnelling_request_frame>(&decoded->payload))
        {
            if (!channel_active(request->channel_id) ||
                !peer_matches(channels_[request->channel_id], peer, true))
                co_return std::unexpected(make_error_code(error::sequence_error));
            auto& channel = channels_[request->channel_id];
            if (channel.incoming_sequence_valid)
            {
                if (request->sequence_number == channel.last_incoming_sequence)
                {
                    response.resize(frame::communication_header_size + frame::tunnelling_ack_size);
                    const auto encoded = frame::encode_tunnelling_ack_packet(
                        response, request->channel_id, request->sequence_number);
                    if (!encoded.has_value())
                        co_return std::unexpected(encoded.error());
                    const auto sent = co_await send_datagram(response, peer);
                    if (!sent)
                        co_return std::unexpected(sent.error());
                    observe_activity(channel);
                    co_return server_event {request->channel_id, {}};
                }
                if (request->sequence_number != channel.next_incoming_sequence)
                    co_return std::unexpected(make_error_code(error::sequence_error));
            }
            channel.incoming_sequence_valid = true;
            channel.last_incoming_sequence = request->sequence_number;
            channel.next_incoming_sequence = static_cast<std::uint8_t>(request->sequence_number + 1u);
            response.resize(frame::communication_header_size + frame::tunnelling_ack_size);
            const auto encoded = frame::encode_tunnelling_ack_packet(
                response, request->channel_id, request->sequence_number);
            if (!encoded.has_value())
                co_return std::unexpected(encoded.error());
            const auto sent = co_await send_datagram(response, peer);
            if (!sent)
                co_return std::unexpected(sent.error());
            observe_activity(channel);
            co_return server_event {
                request->channel_id,
                std::vector<std::uint8_t>(request->cemi_bytes.begin(), request->cemi_bytes.end()),
            };
        }

        co_return std::unexpected(make_error_code(error::unsupported_service));
    }

    task_returning_expected_void_t generic_server::serve() noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (shutdown_)
            co_return std::unexpected(make_error_code(error::shutdown));
        while (!shutdown_)
        {
            if (stop_token.stop_requested())
            {
                shutdown_ = true;
                co_return std::unexpected(make_error_code(error::shutdown));
            }

            const auto event = co_await serve_once();
            if (!event.has_value())
            {
                if (event.error() == make_error_code(error::timeout))
                    continue;
                co_return std::unexpected(event.error());
            }
        }
        co_return expected_void_t {};
    }

    task_returning_expected_void_t generic_server::send(
        const std::uint8_t channel_id, const std::span<const std::uint8_t> cemi_bytes) noexcept(false)
    {
        if (!channel_active(channel_id))
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        if (cemi_bytes.empty())
            co_return std::unexpected(make_error_code(error::malformed_frame));

        std::array<std::uint8_t, frame::max_datagram_size> buffer {};
        const auto encoded = frame::encode_tunnelling_request_packet(
            buffer, channel_id, channels_[channel_id].sequence++, cemi_bytes);
        if (!encoded.has_value())
            co_return std::unexpected(encoded.error());
        const auto size = frame::communication_header_size + frame::tunnelling_request_header_size + cemi_bytes.size();
        std::vector<std::uint8_t> packet(buffer.begin(), buffer.begin() + size);
        const auto sent = co_await send_datagram(packet, channels_[channel_id].data_peer);
        if (!sent)
            co_return std::unexpected(sent.error());
        observe_activity(channels_[channel_id]);
        co_return expected_void_t {};
    }

    expected_void_t generic_server::disconnect(const std::uint8_t channel_id) noexcept
    {
        if (!channel_active(channel_id))
            return std::unexpected(make_error_code(error::invalid_configuration));
        release_channel(channel_id);
        return {};
    }

    expected_void_t generic_server::shutdown() noexcept
    {
        shutdown_ = true;
        for (std::uint16_t id = 1u; id < channels_.size(); ++id)
            release_channel(static_cast<std::uint8_t>(id));
        return {};
    }

    expected_void_t generic_server::reset() noexcept
    {
        for (std::uint16_t id = 1u; id < channels_.size(); ++id)
            release_channel(static_cast<std::uint8_t>(id));
        shutdown_ = false;
        return {};
    }

    expected_void_t generic_server::poll() noexcept
    {
        if (shutdown_)
            return std::unexpected(make_error_code(error::shutdown));
        if (config_.inactivity_timeout_ms == 0u)
            return {};

        const auto current = now_ms();
        for (std::uint16_t id = 1u; id < channels_.size(); ++id)
        {
            const auto channel_id = static_cast<std::uint8_t>(id);
            if (!channels_[channel_id].active)
                continue;
            if (static_cast<std::int32_t>(current - channels_[channel_id].last_activity_ms) >=
                static_cast<std::int32_t>(config_.inactivity_timeout_ms))
                release_channel(channel_id);
        }
        return {};
    }
}
