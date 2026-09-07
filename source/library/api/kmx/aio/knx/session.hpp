/// @file aio/knx/session.hpp
/// @brief Lightweight KNX tunnelling session state machine.
#pragma once
#ifndef PCH
    #include <chrono>
    #include <cstdint>
    #include <expected>
    #include <system_error>
    #include <vector>
#endif

#include <kmx/aio/knx/error.hpp>
#include <kmx/aio/knx/frame.hpp>
#include <kmx/aio/knx/connection.hpp>
#include <kmx/aio/knx/datagram.hpp>

namespace kmx::aio::knx
{
    struct tunnelling_config
    {
        std::uint16_t max_retries = 2u;
        std::uint32_t ack_timeout_ms = 1000u;
        std::uint8_t heartbeat_failure_limit = 3u;
        std::uint32_t inactivity_timeout_ms = 120'000u;
    };

    enum class session_state : std::uint8_t
    {
        idle,
        connecting,
        connected,
        waiting_ack,
        closing,
        closed,
    };

    class tunnelling_session
    {
    public:
        constexpr tunnelling_session() noexcept = default;
        constexpr explicit tunnelling_session(const tunnelling_config& cfg) noexcept : config_(cfg) {}

        constexpr void reset() noexcept
        {
            state_ = session_state::idle;
            pending_ = false;
            connect_retries_ = 0u;
            disconnect_retries_ = 0u;
            retries_ = 0u;
            channel_id_ = 0u;
            expected_sequence_ = 0u;
            next_sequence_ = 0u;
            heartbeat_failures_ = 0u;
            last_activity_ms_ = 0u;
            incoming_sequence_valid_ = false;
            last_incoming_sequence_ = 0u;
            next_incoming_sequence_ = 0u;
            assigned_address_ = individual_address {};
            request_packet_.clear();
            connect_packet_.clear();
            last_deadline_ms_ = 0u;
        }

        [[nodiscard]] constexpr session_state state() const noexcept { return state_; }
        [[nodiscard]] constexpr bool has_pending_request() const noexcept { return pending_; }
        [[nodiscard]] constexpr std::uint32_t retries() const noexcept { return retries_; }
        [[nodiscard]] constexpr std::uint16_t channel_id() const noexcept { return channel_id_; }
        [[nodiscard]] constexpr std::uint16_t expected_sequence() const noexcept { return expected_sequence_; }
        [[nodiscard]] constexpr std::uint8_t next_sequence() const noexcept { return next_sequence_; }
        [[nodiscard]] constexpr std::uint8_t heartbeat_failures() const noexcept { return heartbeat_failures_; }
        [[nodiscard]] constexpr std::uint32_t last_activity_ms() const noexcept { return last_activity_ms_; }
        [[nodiscard]] constexpr std::uint32_t ack_timeout_ms() const noexcept { return config_.ack_timeout_ms; }
        [[nodiscard]] constexpr std::uint32_t deadline_ms() const noexcept { return last_deadline_ms_; }
        [[nodiscard]] constexpr individual_address assigned_address() const noexcept { return assigned_address_; }

        [[nodiscard]] cspan_uint8_t active_request_packet() const noexcept
        {
            return { request_packet_.data(), request_packet_.size() };
        }

        [[nodiscard]] cspan_uint8_t active_connect_packet() const noexcept
        {
            return { connect_packet_.data(), connect_packet_.size() };
        }

        constexpr void shutdown() noexcept
        {
            pending_ = false;
            state_ = session_state::closed;
            request_packet_.clear();
            connect_packet_.clear();
        }

        [[nodiscard]] std::expected<void, std::error_code> prepare_connect_request_packet(
            const span_uint8_t packet, const connect_request_frame& request) noexcept
        {
            if (state_ != session_state::idle)
                return std::unexpected(make_error_code(error::invalid_configuration));

            const auto encoded = connection::encode_connect_request_packet(packet, request);
            if (!encoded.has_value())
                return std::unexpected(encoded.error());

            const auto encoded_length = static_cast<std::size_t>(
                (static_cast<std::uint16_t>(packet[4u]) << 8u) | packet[5u]);
            connect_packet_.assign(packet.begin(), packet.begin() + encoded_length);
            return {};
        }

        [[nodiscard]] std::expected<void, std::error_code> start_connect(
            const span_uint8_t packet, const connect_request_frame& request,
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

        [[nodiscard]] std::expected<void, std::error_code> start_connect_raw(
            const cspan_uint8_t packet, const std::uint32_t deadline_ms) noexcept
        {
            if (state_ != session_state::idle)
                return std::unexpected(make_error_code(error::invalid_configuration));
            if ((packet.size() < frame::communication_header_size) ||
                (packet.size() > frame::max_datagram_size))
                return std::unexpected(make_error_code(error::invalid_length));
            connect_packet_.assign(packet.begin(), packet.end());
            state_ = session_state::connecting;
            connect_retries_ = 0u;
            last_deadline_ms_ = deadline_ms;
            return {};
        }

        [[nodiscard]] std::expected<void, std::error_code> prepare_connect_retry_packet(
            const span_uint8_t packet) const noexcept
        {
            if (state_ != session_state::connecting || connect_packet_.empty() || connect_retries_ == 0u)
                return std::unexpected(make_error_code(error::invalid_configuration));
            if (packet.size() < connect_packet_.size())
                return std::unexpected(make_error_code(error::invalid_length));

            for (std::size_t i = 0u; i < connect_packet_.size(); ++i)
                packet[i] = connect_packet_[i];
            return {};
        }

        [[nodiscard]] std::expected<void, std::error_code> begin_request(const std::uint16_t channel_id,
                                                                         const std::uint16_t sequence,
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

        [[nodiscard]] std::expected<std::uint8_t, std::error_code> begin_request(const std::uint8_t channel_id,
                                                                                 const std::uint32_t deadline_ms) noexcept
        {
            const auto sequence = next_sequence_;
            const auto result = begin_request(channel_id, sequence, deadline_ms);
            if (!result.has_value())
                return std::unexpected(result.error());

            return sequence;
        }

        [[nodiscard]] std::expected<std::uint8_t, std::error_code> prepare_request_packet(
            const span_uint8_t packet,
            const std::uint8_t channel_id,
            const std::span<const std::uint8_t> cemi_bytes,
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

            const auto encoded_length = static_cast<std::size_t>(
                (static_cast<std::uint16_t>(packet[4u]) << 8u) | packet[5u]);
            request_packet_.assign(packet.begin(), packet.begin() + encoded_length);

            const auto started = begin_request(channel_id, sequence, deadline_ms);
            if (!started.has_value())
                return std::unexpected(started.error());

            return sequence;
        }

        [[nodiscard]] std::expected<void, std::error_code> prepare_retry_packet(
            const span_uint8_t packet) const noexcept
        {
            if (!pending_ || (retries_ == 0u) || request_packet_.empty())
                return std::unexpected(make_error_code(error::invalid_configuration));
            if (packet.size() < request_packet_.size())
                return std::unexpected(make_error_code(error::invalid_length));

            for (std::size_t i = 0u; i < request_packet_.size(); ++i)
                packet[i] = request_packet_[i];
            return {};
        }

        [[nodiscard]] std::expected<void, std::error_code> prepare_connectionstate_request_packet(
            const span_uint8_t packet) const noexcept
        {
            if (state_ != session_state::connected)
                return std::unexpected(make_error_code(error::shutdown));

            return connection::encode_connectionstate_request_packet(
                packet, connectionstate_request_frame { static_cast<std::uint8_t>(channel_id_) });
        }

        [[nodiscard]] std::expected<void, std::error_code> prepare_disconnect_request_packet(
            const span_uint8_t packet) noexcept
        {
            if (state_ == session_state::closed)
                return std::unexpected(make_error_code(error::shutdown));
            if (state_ != session_state::connected)
                return std::unexpected(make_error_code(error::invalid_configuration));

            const auto encoded = connection::encode_disconnect_request_packet(
                packet, disconnect_request_frame { static_cast<std::uint8_t>(channel_id_) });
            if (!encoded.has_value())
                return std::unexpected(encoded.error());

            state_ = session_state::closing;
            disconnect_retries_ = 0u;
            return {};
        }

        [[nodiscard]] std::expected<void, std::error_code> on_disconnect_timeout() noexcept
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

        [[nodiscard]] std::expected<void, std::error_code> prepare_tunnelling_ack_packet(
            const span_uint8_t packet, const tunnelling_request_frame& request,
            const std::uint8_t status = 0u) const noexcept
        {
            if ((request.channel_id == 0u) || request.cemi_bytes.empty())
                return std::unexpected(make_error_code(error::malformed_frame));

            return frame::encode_tunnelling_ack_packet(packet,
                                                       request.channel_id,
                                                       request.sequence_number,
                                                       status);
        }

        [[nodiscard]] std::expected<void, std::error_code> prepare_tunnelling_ack_packet(
            const span_uint8_t packet, const cspan_uint8_t request_packet,
            const std::uint8_t status = 0u) const noexcept
        {
            const auto decoded = frame::decode_tunnelling_request_packet(request_packet);
            if (!decoded.has_value())
                return std::unexpected(decoded.error());

            return prepare_tunnelling_ack_packet(packet, decoded.value(), status);
        }

        [[nodiscard]] std::expected<void, std::error_code> prepare_response_datagram(
            const span_uint8_t packet, const datagram& request,
            const std::uint8_t status = 0u) noexcept
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
                if (((state_ != session_state::connected) && (state_ != session_state::closing)) ||
                    (disconnect->channel_id != channel_id_))
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

        [[nodiscard]] std::expected<void, std::error_code> prepare_response_datagram(
            const span_uint8_t packet, const cspan_uint8_t request_packet,
            const std::uint8_t status = 0u) noexcept
        {
            const auto decoded = decode_datagram(request_packet);
            if (!decoded.has_value())
                return std::unexpected(decoded.error());

            return prepare_response_datagram(packet, decoded.value(), status);
        }

        [[nodiscard]] std::expected<void, std::error_code> on_ack(const std::uint16_t channel_id,
                                                                  const std::uint16_t sequence) noexcept
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

        [[nodiscard]] std::expected<void, std::error_code> on_ack(const tunnelling_ack_frame& ack) noexcept
        {
            if (ack.status != 0u)
                return std::unexpected(make_error_code(error::connection_failed));

            return on_ack(ack.channel_id, ack.sequence_number);
        }

        [[nodiscard]] std::expected<void, std::error_code> on_connect_response(
            const connect_response_frame& response) noexcept
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

        [[nodiscard]] std::expected<void, std::error_code> on_connect_response(
            const ipv6_connect_response_frame& response) noexcept
        {
            return on_connect_response(connect_response_frame {
                response.channel_id,
                response.status,
                hpai {},
                response.assigned_address,
            });
        }

        [[nodiscard]] constexpr bool duplicate_indication(const tunnelling_request_frame& request) const noexcept
        {
            return (state_ == session_state::connected) &&
                   (request.channel_id == channel_id_) &&
                   incoming_sequence_valid_ &&
                   (request.sequence_number == last_incoming_sequence_);
        }

        [[nodiscard]] constexpr bool out_of_order_indication(
            const tunnelling_request_frame& request) const noexcept
        {
            return incoming_sequence_valid_ &&
                   (request.sequence_number != next_incoming_sequence_);
        }

        constexpr void note_activity(const std::uint32_t now_ms) noexcept
        {
            if (state_ == session_state::connected)
                last_activity_ms_ = now_ms;
        }

        constexpr void observe_activity(const std::uint32_t now_ms) noexcept
        {
            note_activity(now_ms);
        }

        [[nodiscard]] constexpr bool inactive(const std::uint32_t now_ms) const noexcept
        {
            return (state_ == session_state::connected) &&
                   (static_cast<std::int32_t>(now_ms - last_activity_ms_) >=
                    static_cast<std::int32_t>(config_.inactivity_timeout_ms));
        }

        [[nodiscard]] std::expected<void, std::error_code> check_inactivity(
            const std::uint32_t now_ms) noexcept
        {
            if (!inactive(now_ms))
                return {};

            state_ = session_state::closed;
            pending_ = false;
            return std::unexpected(make_error_code(error::inactivity_timeout));
        }

        [[nodiscard]] std::expected<void, std::error_code> on_connect_response_packet(
            const cspan_uint8_t packet) noexcept
        {
            const auto response = connection::decode_connect_response_packet(packet);
            if (!response.has_value())
                return std::unexpected(response.error());

            return on_connect_response(response.value());
        }

        [[nodiscard]] std::expected<void, std::error_code> on_connect_timeout() noexcept
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

        [[nodiscard]] constexpr bool connect_expired(const std::uint32_t now_ms) const noexcept
        {
            return (state_ == session_state::connecting) &&
                   (static_cast<std::int32_t>(now_ms - last_deadline_ms_) >= 0);
        }

        [[nodiscard]] std::expected<void, std::error_code> on_connectionstate_response(
            const connectionstate_response_frame& response) noexcept
        {
            if (state_ != session_state::connected)
                return std::unexpected(make_error_code(error::invalid_configuration));
            if (response.channel_id != channel_id_)
                return std::unexpected(make_error_code(error::sequence_error));
            if (response.status != connect_status::no_error)
            {
                if (heartbeat_failures_ < 0xFFu)
                    ++heartbeat_failures_;
                if ((config_.heartbeat_failure_limit == 0u) ||
                    (heartbeat_failures_ >= config_.heartbeat_failure_limit))
                {
                    state_ = session_state::closed;
                    return std::unexpected(make_error_code(error::heartbeat_failed));
                }
                return std::unexpected(make_error_code(error::connection_failed));
            }

            heartbeat_failures_ = 0u;
            return {};
        }

        [[nodiscard]] std::expected<void, std::error_code> on_connectionstate_response_packet(
            const cspan_uint8_t packet) noexcept
        {
            const auto response = connection::decode_connectionstate_response_packet(packet);
            if (!response.has_value())
                return std::unexpected(response.error());

            return on_connectionstate_response(response.value());
        }

        [[nodiscard]] std::expected<void, std::error_code> on_connectionstate_timeout() noexcept
        {
            if (state_ != session_state::connected)
                return std::unexpected(make_error_code(error::invalid_configuration));
            if (heartbeat_failures_ < 0xFFu)
                ++heartbeat_failures_;
            if ((config_.heartbeat_failure_limit == 0u) ||
                (heartbeat_failures_ >= config_.heartbeat_failure_limit))
            {
                state_ = session_state::closed;
                return std::unexpected(make_error_code(error::heartbeat_failed));
            }
            return std::unexpected(make_error_code(error::connection_failed));
        }

        [[nodiscard]] std::expected<void, std::error_code> on_ack_packet(const cspan_uint8_t packet) noexcept
        {
            const auto ack = frame::decode_tunnelling_ack_packet(packet);
            if (!ack.has_value())
                return std::unexpected(ack.error());

            return on_ack(ack.value());
        }

        [[nodiscard]] std::expected<void, std::error_code> on_disconnect_response_packet(
            const cspan_uint8_t packet) noexcept
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

        [[nodiscard]] std::expected<void, std::error_code> on_datagram(const datagram& value) noexcept
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

        [[nodiscard]] std::expected<void, std::error_code> dispatch_session_datagram(
            const datagram& value, const std::uint32_t now_ms) noexcept
        {
            const auto result = on_datagram(value);
            if (!result.has_value())
                return result;

            note_activity(now_ms);
            return {};
        }

        [[nodiscard]] std::expected<void, std::error_code> dispatch_session_datagram(
            const cspan_uint8_t packet, const std::uint32_t now_ms) noexcept
        {
            const auto decoded = decode_datagram(packet);
            if (!decoded.has_value())
                return std::unexpected(decoded.error());

            return dispatch_session_datagram(decoded.value(), now_ms);
        }

        [[nodiscard]] std::expected<void, std::error_code> on_datagram(const cspan_uint8_t packet) noexcept
        {
            const auto decoded = decode_datagram(packet);
            if (!decoded.has_value())
                return std::unexpected(decoded.error());

            return on_datagram(decoded.value());
        }

        [[nodiscard]] std::expected<void, std::error_code> on_datagram_at(
            const datagram& value, const std::uint32_t now_ms) noexcept
        {
            return dispatch_session_datagram(value, now_ms);
        }

        [[nodiscard]] std::expected<void, std::error_code> on_datagram_at(
            const cspan_uint8_t packet, const std::uint32_t now_ms) noexcept
        {
            return dispatch_session_datagram(packet, now_ms);
        }

        [[nodiscard]] std::expected<void, std::error_code> on_timeout() noexcept
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

        [[nodiscard]] constexpr bool expired(const std::uint32_t now_ms) const noexcept
        {
            return pending_ && (static_cast<std::int32_t>(now_ms - last_deadline_ms_) >= 0);
        }

    private:
        tunnelling_config config_ {};
        session_state state_ = session_state::idle;
        bool pending_ = false;
        std::uint32_t retries_ = 0u;
        std::uint16_t channel_id_ = 0u;
        std::uint16_t expected_sequence_ = 0u;
        std::uint8_t next_sequence_ = 0u;
        std::uint16_t connect_retries_ = 0u;
        std::uint16_t disconnect_retries_ = 0u;
        std::uint8_t heartbeat_failures_ = 0u;
        std::uint32_t last_activity_ms_ = 0u;
        bool incoming_sequence_valid_ = false;
        std::uint8_t last_incoming_sequence_ = 0u;
        std::uint8_t next_incoming_sequence_ = 0u;
        individual_address assigned_address_ {};
        std::vector<std::uint8_t> request_packet_ {};
        std::vector<std::uint8_t> connect_packet_ {};
        std::uint32_t last_deadline_ms_ = 0u;
    };
}
