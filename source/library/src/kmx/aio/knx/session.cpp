/// @file kmx/aio/knx/session.cpp
/// @brief The compiled body of the KNX tunnelling session state machine.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/session.hpp>

namespace kmx::aio::knx
{
    expected_void_t tunnelling_session::prepare_connect_request_packet(const span_uint8_t packet,
                                                                                            const connect_request_frame& request) noexcept
    {
        if (state_ != session_state::idle)
            return std::unexpected(make_error_code(error::invalid_configuration));

        const auto encoded = connection::encode_connect_request_packet(packet, request);
        if (!encoded.has_value())
            return std::unexpected(encoded.error());

        const auto encoded_length = static_cast<std::size_t>((static_cast<std::uint16_t>(packet[4u]) << 8u) | packet[5u]);
        connect_packet_.assign(packet.begin(), packet.begin() + encoded_length);
        return {};
    }

    expected_void_t tunnelling_session::start_connect(const span_uint8_t packet, const connect_request_frame& request,
                                                                           const std::uint32_t deadline_ms) noexcept
    {
        if (state_ != session_state::idle)
            return std::unexpected(make_error_code(error::invalid_configuration));

        const auto prepared = prepare_connect_request_packet(packet, request);
        if (!prepared.has_value())
            return std::unexpected(prepared.error());

        state_ = session_state::connecting;
        connect_retries_ = 0u;
        last_deadline_ms_ = deadline_ms;
        return {};
    }

    expected_void_t tunnelling_session::start_connect_raw(const cspan_uint8_t packet,
                                                                               const std::uint32_t deadline_ms) noexcept
    {
        if (state_ != session_state::idle)
            return std::unexpected(make_error_code(error::invalid_configuration));
        if ((packet.size() < frame::communication_header_size) || (packet.size() > frame::max_datagram_size))
            return std::unexpected(make_error_code(error::invalid_length));
        connect_packet_.assign(packet.begin(), packet.end());
        state_ = session_state::connecting;
        connect_retries_ = 0u;
        last_deadline_ms_ = deadline_ms;
        return {};
    }

    expected_void_t tunnelling_session::prepare_connect_retry_packet(const span_uint8_t packet) const noexcept
    {
        if ((state_ != session_state::connecting) || connect_packet_.empty() || (connect_retries_ == 0u))
            return std::unexpected(make_error_code(error::invalid_configuration));
        if (packet.size() < connect_packet_.size())
            return std::unexpected(make_error_code(error::invalid_length));

        for (std::size_t i = 0u; i < connect_packet_.size(); ++i)
            packet[i] = connect_packet_[i];
        return {};
    }

    expected_void_t tunnelling_session::begin_request(const std::uint16_t channel_id, const std::uint16_t sequence,
                                                                           const std::uint32_t deadline_ms) noexcept
    {
        if (pending_)
            return std::unexpected(make_error_code(error::send_queue_full));
        if ((state_ == session_state::closing) || (state_ == session_state::closed))
            return std::unexpected(make_error_code(error::shutdown));
        if ((channel_id == 0u) || (channel_id > 0xFFu) || (sequence > 0xFFu))
            return std::unexpected(make_error_code(error::sequence_error));

        pending_ = true;
        state_ = session_state::waiting_ack;
        channel_id_ = static_cast<std::uint8_t>(channel_id);
        expected_sequence_ = static_cast<std::uint8_t>(sequence);
        next_sequence_ = static_cast<std::uint8_t>(expected_sequence_ + 1u);
        last_deadline_ms_ = deadline_ms;
        return {};
    }

    std::expected<std::uint8_t, std::error_code> tunnelling_session::begin_request(const std::uint8_t channel_id,
                                                                                   const std::uint32_t deadline_ms) noexcept
    {
        const auto sequence = next_sequence_;
        const auto result = begin_request(channel_id, sequence, deadline_ms);
        if (!result.has_value())
            return std::unexpected(result.error());

        return sequence;
    }

    std::expected<std::uint8_t, std::error_code> tunnelling_session::prepare_request_packet(const span_uint8_t packet,
                                                                                            const std::uint8_t channel_id,
                                                                                            const cspan_uint8_t cemi_bytes,
                                                                                            const std::uint32_t deadline_ms) noexcept
    {
        if (pending_)
            return std::unexpected(make_error_code(error::send_queue_full));
        if ((state_ == session_state::closing) || (state_ == session_state::closed))
            return std::unexpected(make_error_code(error::shutdown));
        if (channel_id == 0u)
            return std::unexpected(make_error_code(error::sequence_error));

        const auto sequence = next_sequence_;
        const auto encoded = frame::encode_tunnelling_request_packet(packet, channel_id, sequence, cemi_bytes);
        if (!encoded.has_value())
            return std::unexpected(encoded.error());

        const auto encoded_length = static_cast<std::size_t>((static_cast<std::uint16_t>(packet[4u]) << 8u) | packet[5u]);
        request_packet_.assign(packet.begin(), packet.begin() + encoded_length);

        const auto started = begin_request(channel_id, sequence, deadline_ms);
        if (!started.has_value())
            return std::unexpected(started.error());

        return sequence;
    }

    expected_void_t tunnelling_session::prepare_retry_packet(const span_uint8_t packet) const noexcept
    {
        if (!pending_ || (retries_ == 0u) || request_packet_.empty())
            return std::unexpected(make_error_code(error::invalid_configuration));
        if (packet.size() < request_packet_.size())
            return std::unexpected(make_error_code(error::invalid_length));

        for (std::size_t i = 0u; i < request_packet_.size(); ++i)
            packet[i] = request_packet_[i];
        return {};
    }

    expected_void_t tunnelling_session::prepare_connectionstate_request_packet(const span_uint8_t packet) const noexcept
    {
        if (state_ != session_state::connected)
            return std::unexpected(make_error_code(error::shutdown));

        return connection::encode_connectionstate_request_packet(packet,
                                                                 connectionstate_request_frame {static_cast<std::uint8_t>(channel_id_)});
    }

    expected_void_t tunnelling_session::prepare_disconnect_request_packet(const span_uint8_t packet) noexcept
    {
        if (state_ == session_state::closed)
            return std::unexpected(make_error_code(error::shutdown));
        if (state_ != session_state::connected)
            return std::unexpected(make_error_code(error::invalid_configuration));

        const auto encoded =
            connection::encode_disconnect_request_packet(packet, disconnect_request_frame {static_cast<std::uint8_t>(channel_id_)});
        if (!encoded.has_value())
            return std::unexpected(encoded.error());

        state_ = session_state::closing;
        disconnect_retries_ = 0u;
        return {};
    }

    expected_void_t tunnelling_session::on_disconnect_timeout() noexcept
    {
        if (state_ != session_state::closing)
            return std::unexpected(make_error_code(error::invalid_configuration));
        ++disconnect_retries_;
        if (disconnect_retries_ > config_.max_retries)
        {
            state_ = session_state::closed;
            return std::unexpected(make_error_code(error::timeout));
        }
        return {};
    }

    expected_void_t tunnelling_session::prepare_tunnelling_ack_packet(const span_uint8_t packet,
                                                                                           const tunnelling_request_frame& request,
                                                                                           const std::uint8_t status) const noexcept
    {
        if ((request.channel_id == 0u) || request.cemi_bytes.empty())
            return std::unexpected(make_error_code(error::malformed_frame));

        return frame::encode_tunnelling_ack_packet(packet, request.channel_id, request.sequence_number, status);
    }

    expected_void_t tunnelling_session::prepare_tunnelling_ack_packet(const span_uint8_t packet,
                                                                                           const cspan_uint8_t request_packet,
                                                                                           const std::uint8_t status) const noexcept
    {
        const auto decoded = frame::decode_tunnelling_request_packet(request_packet);
        if (!decoded.has_value())
            return std::unexpected(decoded.error());

        return prepare_tunnelling_ack_packet(packet, decoded.value(), status);
    }

    expected_void_t tunnelling_session::prepare_response_datagram(const span_uint8_t packet, const datagram& request,
                                                                                       const std::uint8_t status) noexcept
    {
        if (const auto* tunnel = std::get_if<tunnelling_request_frame>(&request.payload))
        {
            if (request.service_type != frame::tunnelling_request_service)
                return std::unexpected(make_error_code(error::invalid_configuration));
            if (state_ != session_state::connected)
                return std::unexpected(make_error_code(error::invalid_configuration));
            if (tunnel->channel_id != channel_id_)
                return std::unexpected(make_error_code(error::sequence_error));
        }
        else if (const auto* heartbeat = std::get_if<connectionstate_request_frame>(&request.payload))
        {
            if (request.service_type != connection::connectionstate_request_service)
                return std::unexpected(make_error_code(error::invalid_configuration));
            if ((state_ != session_state::connected) || (heartbeat->channel_id != channel_id_))
                return std::unexpected(make_error_code(error::sequence_error));
        }
        else if (const auto* disconnect = std::get_if<disconnect_request_frame>(&request.payload))
        {
            if (request.service_type != connection::disconnect_request_service)
                return std::unexpected(make_error_code(error::invalid_configuration));
            if (((state_ != session_state::connected) && (state_ != session_state::closing)) || (disconnect->channel_id != channel_id_))
                return std::unexpected(make_error_code(error::sequence_error));
        }

        const auto encoded = encode_response_datagram(packet, request, status);
        if (!encoded.has_value())
            return std::unexpected(encoded.error());

        const auto accepted = on_datagram(request);
        if (!accepted.has_value())
            return std::unexpected(accepted.error());

        return {};
    }

    expected_void_t tunnelling_session::prepare_response_datagram(const span_uint8_t packet,
                                                                                       const cspan_uint8_t request_packet,
                                                                                       const std::uint8_t status) noexcept
    {
        const auto decoded = decode_datagram(request_packet);
        if (!decoded.has_value())
            return std::unexpected(decoded.error());

        return prepare_response_datagram(packet, decoded.value(), status);
    }

    expected_void_t tunnelling_session::on_ack(const std::uint16_t channel_id, const std::uint16_t sequence) noexcept
    {
        if (!pending_)
            return std::unexpected(make_error_code(error::sequence_error));

        if ((channel_id_ != channel_id) || (expected_sequence_ != sequence))
            return std::unexpected(make_error_code(error::sequence_error));

        pending_ = false;
        request_packet_.clear();
        state_ = session_state::connected;
        retries_ = 0u;
        channel_id_ = channel_id;
        expected_sequence_ = sequence;
        return {};
    }

    expected_void_t tunnelling_session::on_ack(const tunnelling_ack_frame& ack) noexcept
    {
        if (ack.status != 0u)
            return std::unexpected(make_error_code(error::connection_failed));

        return on_ack(ack.channel_id, ack.sequence_number);
    }

    expected_void_t tunnelling_session::on_connect_response(const connect_response_frame& response) noexcept
    {
        if ((state_ != session_state::idle) && (state_ != session_state::connecting))
            return std::unexpected(make_error_code(error::invalid_configuration));
        if (response.status != connect_status::no_error)
        {
            if (state_ == session_state::connecting)
            {
                state_ = session_state::idle;
                connect_packet_.clear();
                connect_retries_ = 0u;
            }
            return std::unexpected(make_error_code(error::connection_failed));
        }
        if (response.channel_id == 0u)
        {
            if (state_ == session_state::connecting)
            {
                state_ = session_state::idle;
                connect_packet_.clear();
                connect_retries_ = 0u;
            }
            return std::unexpected(make_error_code(error::sequence_error));
        }

        channel_id_ = response.channel_id;
        assigned_address_ = response.assigned_address;
        state_ = session_state::connected;
        pending_ = false;
        retries_ = 0u;
        connect_retries_ = 0u;
        disconnect_retries_ = 0u;
        connect_packet_.clear();
        heartbeat_failures_ = 0u;
        last_activity_ms_ = 0u;
        incoming_sequence_valid_ = false;
        return {};
    }

    expected_void_t tunnelling_session::on_connect_response(const ipv6_connect_response_frame& response) noexcept
    {
        return on_connect_response(connect_response_frame {
            response.channel_id,
            response.status,
            hpai {},
            response.assigned_address,
        });
    }

    expected_void_t tunnelling_session::check_inactivity(const std::uint32_t now_ms) noexcept
    {
        if (!inactive(now_ms))
            return {};

        state_ = session_state::closed;
        pending_ = false;
        return std::unexpected(make_error_code(error::inactivity_timeout));
    }

    expected_void_t tunnelling_session::on_connect_response_packet(const cspan_uint8_t packet) noexcept
    {
        const auto response = connection::decode_connect_response_packet(packet);
        if (!response.has_value())
            return std::unexpected(response.error());

        return on_connect_response(response.value());
    }

    expected_void_t tunnelling_session::on_connect_timeout() noexcept
    {
        if (state_ != session_state::connecting)
            return std::unexpected(make_error_code(error::invalid_configuration));

        ++connect_retries_;
        if (connect_retries_ > config_.max_retries)
        {
            state_ = session_state::closed;
            connect_packet_.clear();
            return std::unexpected(make_error_code(error::timeout));
        }

        last_deadline_ms_ += config_.ack_timeout_ms;
        return {};
    }

    expected_void_t tunnelling_session::on_connectionstate_response(const connectionstate_response_frame& response) noexcept
    {
        if (state_ != session_state::connected)
            return std::unexpected(make_error_code(error::invalid_configuration));
        if (response.channel_id != channel_id_)
            return std::unexpected(make_error_code(error::sequence_error));
        if (response.status != connect_status::no_error)
        {
            if (heartbeat_failures_ < 0xFFu)
                ++heartbeat_failures_;
            if ((config_.heartbeat_failure_limit == 0u) || (heartbeat_failures_ >= config_.heartbeat_failure_limit))
            {
                state_ = session_state::closed;
                return std::unexpected(make_error_code(error::heartbeat_failed));
            }
            return std::unexpected(make_error_code(error::connection_failed));
        }

        heartbeat_failures_ = 0u;
        return {};
    }

    expected_void_t tunnelling_session::on_connectionstate_response_packet(const cspan_uint8_t packet) noexcept
    {
        const auto response = connection::decode_connectionstate_response_packet(packet);
        if (!response.has_value())
            return std::unexpected(response.error());

        return on_connectionstate_response(response.value());
    }

    expected_void_t tunnelling_session::on_connectionstate_timeout() noexcept
    {
        if (state_ != session_state::connected)
            return std::unexpected(make_error_code(error::invalid_configuration));
        if (heartbeat_failures_ < 0xFFu)
            ++heartbeat_failures_;
        if ((config_.heartbeat_failure_limit == 0u) || (heartbeat_failures_ >= config_.heartbeat_failure_limit))
        {
            state_ = session_state::closed;
            return std::unexpected(make_error_code(error::heartbeat_failed));
        }
        return std::unexpected(make_error_code(error::connection_failed));
    }

    expected_void_t tunnelling_session::on_ack_packet(const cspan_uint8_t packet) noexcept
    {
        const auto ack = frame::decode_tunnelling_ack_packet(packet);
        if (!ack.has_value())
            return std::unexpected(ack.error());

        return on_ack(ack.value());
    }

    expected_void_t tunnelling_session::on_disconnect_response_packet(const cspan_uint8_t packet) noexcept
    {
        if (state_ == session_state::closed)
            return std::unexpected(make_error_code(error::shutdown));
        if (state_ != session_state::closing)
            return std::unexpected(make_error_code(error::invalid_configuration));

        const auto response = connection::decode_disconnect_response_packet(packet);
        if (!response.has_value())
            return std::unexpected(response.error());
        if (response->channel_id != channel_id_)
            return std::unexpected(make_error_code(error::sequence_error));
        if (response->status != connect_status::no_error)
            return std::unexpected(make_error_code(error::connection_failed));

        shutdown();
        return {};
    }

    expected_void_t tunnelling_session::on_datagram(const datagram& value) noexcept
    {
        if (const auto* response = std::get_if<connect_response_frame>(&value.payload))
        {
            if (value.service_type != connection::connect_response_service)
                return std::unexpected(make_error_code(error::invalid_configuration));
            return on_connect_response(*response);
        }
        if (const auto* response = std::get_if<ipv6_connect_response_frame>(&value.payload))
        {
            if (value.service_type != connection::connect_response_service)
                return std::unexpected(make_error_code(error::invalid_configuration));
            return on_connect_response(*response);
        }
        if (const auto* response = std::get_if<connectionstate_response_frame>(&value.payload))
        {
            if (value.service_type != connection::connectionstate_response_service)
                return std::unexpected(make_error_code(error::invalid_configuration));
            return on_connectionstate_response(*response);
        }
        if (const auto* ack = std::get_if<tunnelling_ack_frame>(&value.payload))
        {
            if (value.service_type != frame::tunnelling_ack_service)
                return std::unexpected(make_error_code(error::invalid_configuration));
            return on_ack(*ack);
        }
        if (const auto* request = std::get_if<tunnelling_request_frame>(&value.payload))
        {
            if (value.service_type != frame::tunnelling_request_service)
                return std::unexpected(make_error_code(error::invalid_configuration));
            if (state_ != session_state::connected)
                return std::unexpected(make_error_code(error::invalid_configuration));
            if (request->channel_id != channel_id_)
                return std::unexpected(make_error_code(error::sequence_error));
            if (request->cemi_bytes.empty())
                return std::unexpected(make_error_code(error::malformed_frame));
            if (duplicate_indication(*request))
                return std::unexpected(make_error_code(error::sequence_error));
            if (out_of_order_indication(*request))
                return std::unexpected(make_error_code(error::sequence_error));
            incoming_sequence_valid_ = true;
            last_incoming_sequence_ = request->sequence_number;
            next_incoming_sequence_ = static_cast<std::uint8_t>(request->sequence_number + 1u);
            return {};
        }
        if (const auto* request = std::get_if<connectionstate_request_frame>(&value.payload))
        {
            if (value.service_type != connection::connectionstate_request_service)
                return std::unexpected(make_error_code(error::invalid_configuration));
            if (state_ != session_state::connected)
                return std::unexpected(make_error_code(error::invalid_configuration));
            if (request->channel_id != channel_id_)
                return std::unexpected(make_error_code(error::sequence_error));
            return {};
        }
        if (const auto* request = std::get_if<disconnect_request_frame>(&value.payload))
        {
            if (value.service_type != connection::disconnect_request_service)
                return std::unexpected(make_error_code(error::invalid_configuration));
            if ((state_ != session_state::connected) && (state_ != session_state::closing))
                return std::unexpected(make_error_code(error::invalid_configuration));
            if (request->channel_id != channel_id_)
                return std::unexpected(make_error_code(error::sequence_error));
            state_ = session_state::closing;
            return {};
        }
        if (const auto* response = std::get_if<disconnect_response_frame>(&value.payload))
        {
            if (value.service_type != connection::disconnect_response_service)
                return std::unexpected(make_error_code(error::invalid_configuration));
            if (state_ == session_state::closed)
                return std::unexpected(make_error_code(error::shutdown));
            if (state_ != session_state::closing)
                return std::unexpected(make_error_code(error::invalid_configuration));
            if (response->channel_id != channel_id_)
                return std::unexpected(make_error_code(error::sequence_error));
            if (response->status != connect_status::no_error)
                return std::unexpected(make_error_code(error::connection_failed));

            shutdown();
            return {};
        }

        return std::unexpected(make_error_code(error::unsupported_service));
    }

    expected_void_t tunnelling_session::dispatch_session_datagram(const datagram& value,
                                                                                       const std::uint32_t now_ms) noexcept
    {
        const auto result = on_datagram(value);
        if (!result.has_value())
            return result;

        note_activity(now_ms);
        return {};
    }

    expected_void_t tunnelling_session::dispatch_session_datagram(const cspan_uint8_t packet,
                                                                                       const std::uint32_t now_ms) noexcept
    {
        const auto decoded = decode_datagram(packet);
        if (!decoded.has_value())
            return std::unexpected(decoded.error());

        return dispatch_session_datagram(decoded.value(), now_ms);
    }

    expected_void_t tunnelling_session::on_datagram(const cspan_uint8_t packet) noexcept
    {
        const auto decoded = decode_datagram(packet);
        if (!decoded.has_value())
            return std::unexpected(decoded.error());

        return on_datagram(decoded.value());
    }

    expected_void_t tunnelling_session::on_timeout() noexcept
    {
        if (!pending_)
            return std::unexpected(make_error_code(error::invalid_configuration));

        ++retries_;
        if (retries_ > config_.max_retries)
        {
            pending_ = false;
            request_packet_.clear();
            state_ = session_state::closed;
            return std::unexpected(make_error_code(error::timeout));
        }

        last_deadline_ms_ += config_.ack_timeout_ms;
        state_ = session_state::waiting_ack;
        return {};
    }
}
