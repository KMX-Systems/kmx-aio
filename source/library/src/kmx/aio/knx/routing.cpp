/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/routing.hpp>

#include <chrono>

namespace kmx::aio::knx::routing
{
    namespace
    {
        [[nodiscard]] std::expected<std::uint16_t, std::error_code> decode_control_value(
            const cspan_uint8_t packet, const std::uint16_t service) noexcept
        {
            const auto header = frame::decode_communication_header(packet);
            if (!header.has_value())
                return std::unexpected(header.error());
            if (header->service_type != service)
                return std::unexpected(make_error_code(error::unsupported_service));
            if ((header->total_length != packet.size()) ||
                (packet.size() != frame::communication_header_size + control_body_size))
                return std::unexpected(make_error_code(error::malformed_frame));
            return static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(packet[frame::communication_header_size]) << 8u) |
                static_cast<std::uint16_t>(packet[frame::communication_header_size + 1u]));
        }

        [[nodiscard]] std::expected<void, std::error_code> encode_control_value(
            const span_uint8_t destination, const std::uint16_t service, const std::uint16_t value) noexcept
        {
            const auto total_length = frame::communication_header_size + control_body_size;
            if (destination.size() < total_length)
                return std::unexpected(make_error_code(error::invalid_length));
            const auto header = frame::encode_communication_header(
                destination, service, static_cast<std::uint16_t>(total_length));
            if (!header.has_value())
                return std::unexpected(header.error());
            destination[frame::communication_header_size] = static_cast<std::uint8_t>(value >> 8u);
            destination[frame::communication_header_size + 1u] = static_cast<std::uint8_t>(value & 0xFFu);
            return {};
        }
    }

    std::expected<void, std::error_code> encode_indication_packet(
        const span_uint8_t destination, const indication& value) noexcept
    {
        if ((value.channel_id == 0u) || value.cemi_bytes.empty())
            return std::unexpected(make_error_code(error::invalid_configuration));
        const auto total_length = frame::communication_header_size + indication_header_size + value.cemi_bytes.size();
        if ((total_length > frame::max_frame_size) || (destination.size() < total_length))
            return std::unexpected(make_error_code(error::invalid_length));
        const auto header = frame::encode_communication_header(
            destination, indication_service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());
        destination[6u] = value.channel_id;
        destination[7u] = 0u;
        destination[8u] = 0u;
        destination[9u] = 0u;
        for (std::size_t i = 0u; i < value.cemi_bytes.size(); ++i)
            destination[frame::communication_header_size + indication_header_size + i] = value.cemi_bytes[i];
        return {};
    }

    std::expected<indication, std::error_code> decode_indication_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->service_type != indication_service) || (header->total_length != packet.size()))
            return std::unexpected(make_error_code(error::unsupported_service));
        if (packet.size() < frame::communication_header_size + indication_header_size + frame::cemi_min_size)
            return std::unexpected(make_error_code(error::malformed_frame));
        if ((packet[7u] != 0u) || (packet[8u] != 0u) || (packet[9u] != 0u))
            return std::unexpected(make_error_code(error::malformed_frame));
        const auto cemi = frame::decode_cemi(packet.subspan(frame::communication_header_size + indication_header_size));
        if (!cemi.has_value())
            return std::unexpected(cemi.error());
        return indication {packet[6u], packet.subspan(frame::communication_header_size + indication_header_size)};
    }

    std::expected<void, std::error_code> encode_lost_message_packet(
        const span_uint8_t destination, const lost_message& value) noexcept
    {
        return encode_control_value(destination, lost_message_service, value.count);
    }

    std::expected<lost_message, std::error_code> decode_lost_message_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto value = decode_control_value(packet, lost_message_service);
        if (!value.has_value())
            return std::unexpected(value.error());
        return lost_message {*value};
    }

    std::expected<void, std::error_code> encode_busy_packet(
        const span_uint8_t destination, const busy& value) noexcept
    {
        return encode_control_value(destination, busy_service, value.wait_time_ms);
    }

    std::expected<busy, std::error_code> decode_busy_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto value = decode_control_value(packet, busy_service);
        if (!value.has_value())
            return std::unexpected(value.error());
        return busy {*value};
    }

    std::expected<socket_address, std::error_code> client::multicast_peer() const noexcept
    {
        const auto group = ipv4::address_t {
            configuration_.group.data(),
            configuration_.group.size(),
        };
        return make_socket_address(group, configuration_.port);
    }

    bool client::valid_source_peer(const transport_peer& peer) noexcept
    {
        if ((peer.length == 0u) || (peer.length > sizeof(sockaddr_storage)))
            return false;
        if (peer.address.ss_family == AF_INET)
        {
            if (peer.length < sizeof(sockaddr_in))
                return false;
            return reinterpret_cast<const sockaddr_in&>(peer.address).sin_port != 0u;
        }
        if (peer.address.ss_family == AF_INET6)
        {
            if (peer.length < sizeof(sockaddr_in6))
                return false;
            return reinterpret_cast<const sockaddr_in6&>(peer.address).sin6_port != 0u;
        }
        return false;
    }

    std::uint32_t client::now_ms() const noexcept
    {
        if (clock_now_ != nullptr)
            return clock_now_();
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    expected_void_t client::start() noexcept
    {
        if (started_)
            return {};

        const auto valid = validate(configuration_);
        if (!valid.has_value())
            return std::unexpected(make_error_code(valid.error()));

        const auto joined = transport_.join_multicast_group(configuration_);
        if (!joined.has_value())
            return std::unexpected(joined.error());

        started_ = true;
        return {};
    }

    expected_void_t client::stop() noexcept
    {
        if (!started_)
            return {};

        const auto left = transport_.leave_multicast_group(configuration_);
        if (!left.has_value())
            return std::unexpected(left.error());

        started_ = false;
        last_sent_packet_.clear();
        busy_until_ms_ = 0u;
        return {};
    }

    void client::note_busy(const std::uint32_t backoff_ms) noexcept
    {
        ++counters_.busy_messages;
        counters_.busy_backoff_ms = backoff_ms;
        busy_until_ms_ = now_ms() + backoff_ms;
    }

    void client::note_lost() noexcept
    {
        ++counters_.lost_messages;
    }

    void client::note_reflected() noexcept
    {
        ++counters_.reflected_messages;
    }

    task_returning_expected_void_t client::send_indication(const indication& value) noexcept(false)
    {
        if (!started_)
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        if (static_cast<std::int32_t>(busy_until_ms_ - now_ms()) > 0)
            co_return std::unexpected(make_error_code(error::timeout));

        std::array<std::uint8_t, frame::max_datagram_size> packet {};
        const auto encoded = encode_indication_packet(packet, value);
        if (!encoded.has_value())
            co_return std::unexpected(encoded.error());

        const auto packet_size = frame::communication_header_size + indication_header_size + value.cemi_bytes.size();
        const auto destination = multicast_peer();
        if (!destination.has_value())
            co_return std::unexpected(destination.error());

        const auto* bytes = reinterpret_cast<const std::byte*>(packet.data());
        const auto sent = co_await transport_.send(
            cspan_byte_t {bytes, packet_size},
            reinterpret_cast<const sockaddr*>(&destination->storage),
            destination->length);
        if (!sent.has_value())
            co_return std::unexpected(sent.error());
        if (*sent != packet_size)
            co_return std::unexpected(make_error_code(error::connection_failed));

        last_sent_packet_.assign(packet.begin(), packet.begin() + packet_size);

        co_return expected_void_t {};
    }

    task_returning_expected_void_t client::send_busy(const busy& value) noexcept(false)
    {
        if (!started_)
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        std::array<std::uint8_t, frame::communication_header_size + control_body_size> packet {};
        const auto encoded = encode_busy_packet(packet, value);
        if (!encoded.has_value())
            co_return std::unexpected(encoded.error());
        const auto destination = multicast_peer();
        if (!destination.has_value())
            co_return std::unexpected(destination.error());
        const auto* bytes = reinterpret_cast<const std::byte*>(packet.data());
        const auto sent = co_await transport_.send(
            cspan_byte_t {bytes, packet.size()},
            reinterpret_cast<const sockaddr*>(&destination->storage), destination->length);
        if (!sent)
            co_return std::unexpected(sent.error());
        if (*sent != packet.size())
            co_return std::unexpected(make_error_code(error::connection_failed));
        co_return expected_void_t {};
    }

    task_returning_expected_void_t client::send_lost_message(const lost_message& value) noexcept(false)
    {
        if (!started_)
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        std::array<std::uint8_t, frame::communication_header_size + control_body_size> packet {};
        const auto encoded = encode_lost_message_packet(packet, value);
        if (!encoded.has_value())
            co_return std::unexpected(encoded.error());
        const auto destination = multicast_peer();
        if (!destination.has_value())
            co_return std::unexpected(destination.error());
        const auto* bytes = reinterpret_cast<const std::byte*>(packet.data());
        const auto sent = co_await transport_.send(
            cspan_byte_t {bytes, packet.size()},
            reinterpret_cast<const sockaddr*>(&destination->storage), destination->length);
        if (!sent)
            co_return std::unexpected(sent.error());
        if (*sent != packet.size())
            co_return std::unexpected(make_error_code(error::connection_failed));
        co_return expected_void_t {};
    }

    task<std::expected<event, std::error_code>> client::receive_event() noexcept(false)
    {
        if (!started_)
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        for (;;)
        {
            transport_peer peer {};
            auto* bytes = reinterpret_cast<std::byte*>(receive_buffer_.data());
            const auto received = co_await transport_.receive(
                span_byte_t {bytes, receive_buffer_.size()}, peer);
            if (!received.has_value())
                co_return std::unexpected(received.error());
            if (!valid_source_peer(peer))
                co_return std::unexpected(make_error_code(error::connection_failed));
            if ((received.value() == 0u) || (received.value() > receive_buffer_.size()))
                co_return std::unexpected(make_error_code(error::invalid_length));

            const cspan_uint8_t packet {receive_buffer_.data(), received.value()};
            if (!last_sent_packet_.empty() &&
                (packet.size() == last_sent_packet_.size()) &&
                std::equal(packet.begin(), packet.end(), last_sent_packet_.begin()))
            {
                note_reflected();
                continue;
            }

            const auto header = frame::decode_communication_header(packet);
            if (!header.has_value())
                co_return std::unexpected(header.error());

            switch (header->service_type)
            {
            case indication_service:
            {
                const auto decoded = decode_indication_packet(packet);
                if (!decoded.has_value())
                    co_return std::unexpected(decoded.error());
                co_return event {received_indication {
                    .channel_id = decoded->channel_id,
                    .cemi_bytes = std::vector<std::uint8_t>(decoded->cemi_bytes.begin(), decoded->cemi_bytes.end()),
                }};
            }
            case busy_service:
            {
                const auto decoded = decode_busy_packet(packet);
                if (!decoded.has_value())
                    co_return std::unexpected(decoded.error());
                note_busy(decoded->wait_time_ms);
                co_return event {decoded.value()};
            }
            case lost_message_service:
            {
                const auto decoded = decode_lost_message_packet(packet);
                if (!decoded.has_value())
                    co_return std::unexpected(decoded.error());
                counters_.lost_messages += decoded->count;
                co_return event {decoded.value()};
            }
                default:
                    co_return std::unexpected(make_error_code(error::unsupported_service));
            }
        }
    }

    task<std::expected<received_indication, std::error_code>> client::receive_indication() noexcept(false)
    {
        for (;;)
        {
            const auto received = co_await receive_event();
            if (!received.has_value())
                co_return std::unexpected(received.error());
            if (const auto* indication = std::get_if<received_indication>(&received.value()); indication != nullptr)
                co_return *indication;
        }
    }
}