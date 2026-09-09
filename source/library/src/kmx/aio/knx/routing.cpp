/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/routing.hpp>

#include <algorithm>
#include <chrono>

namespace kmx::aio::knx::routing
{
    /// @brief Reads a big-endian 16-bit field.
    [[nodiscard]] static constexpr std::uint16_t decode_u16_be(const cspan_uint8_t value, const std::size_t offset) noexcept
    {
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(value[offset]) << 8u) |
                                          static_cast<std::uint16_t>(value[offset + 1u]));
    }

    /// @brief Writes a big-endian 16-bit field.
    static constexpr void encode_u16_be(const span_uint8_t destination, const std::size_t offset, const std::uint16_t value) noexcept
    {
        destination[offset] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
        destination[offset + 1u] = static_cast<std::uint8_t>(value & 0xFFu);
    }

    /// @brief Validates the header and the information block prologue the two control services share.
    /// @param packet The received datagram.
    /// @param service The service type expected.
    /// @param body_size The exact body size that service defines.
    /// @return Nothing on success, or why the datagram is not that service.
    /// @details Both control services carry a single information block whose first octet repeats its own
    ///          size. A block that disagrees with the datagram it arrived in is rejected rather than read.
    [[nodiscard]] static expected_void_t validate_control_packet(const cspan_uint8_t packet, const std::uint16_t service,
                                                                 const std::size_t body_size) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if (header->service_type != service)
            return std::unexpected(make_error_code(error::unsupported_service));
        if ((header->total_length != packet.size()) || (packet.size() != frame::communication_header_size + body_size))
            return std::unexpected(make_error_code(error::malformed_frame));
        if (packet[frame::communication_header_size] != static_cast<std::uint8_t>(body_size))
            return std::unexpected(make_error_code(error::malformed_frame));
        return {};
    }

    /// @brief Writes the header and information block prologue the two control services share.
    [[nodiscard]] static expected_void_t begin_control_packet(const span_uint8_t destination, const std::uint16_t service,
                                                              const std::size_t body_size, const std::uint8_t device_state) noexcept
    {
        const auto total_length = frame::communication_header_size + body_size;
        if (destination.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));
        const auto header = frame::encode_communication_header(destination, service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());
        destination[frame::communication_header_size] = static_cast<std::uint8_t>(body_size);
        destination[frame::communication_header_size + 1u] = device_state;
        return {};
    }

    expected_void_t encode_indication_packet(const span_uint8_t destination, const indication& value) noexcept
    {
        if (value.cemi_bytes.empty())
            return std::unexpected(make_error_code(error::invalid_configuration));
        const auto total_length = frame::communication_header_size + value.cemi_bytes.size();
        if ((total_length > frame::max_frame_size) || (destination.size() < total_length))
            return std::unexpected(make_error_code(error::invalid_length));
        const auto header = frame::encode_communication_header(
            destination, indication_service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());
        std::copy_n(value.cemi_bytes.begin(), value.cemi_bytes.size(), destination.begin() + frame::communication_header_size);
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
        if (packet.size() < frame::communication_header_size + frame::cemi_min_size)
            return std::unexpected(make_error_code(error::malformed_frame));
        const auto cemi_bytes = packet.subspan(frame::communication_header_size);
        const auto cemi = frame::decode_cemi(cemi_bytes);
        if (!cemi.has_value())
            return std::unexpected(cemi.error());
        return indication {cemi_bytes, cemi.value()};
    }

    expected_void_t encode_lost_message_packet(const span_uint8_t destination, const lost_message& value) noexcept
    {
        const auto begun = begin_control_packet(destination, lost_message_service, lost_message_body_size, value.device_state);
        if (!begun.has_value())
            return std::unexpected(begun.error());
        encode_u16_be(destination, frame::communication_header_size + 2u, value.count);
        return {};
    }

    std::expected<lost_message, std::error_code> decode_lost_message_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto valid = validate_control_packet(packet, lost_message_service, lost_message_body_size);
        if (!valid.has_value())
            return std::unexpected(valid.error());
        return lost_message {
            .device_state = packet[frame::communication_header_size + 1u],
            .count = decode_u16_be(packet, frame::communication_header_size + 2u),
        };
    }

    expected_void_t encode_busy_packet(const span_uint8_t destination, const busy& value) noexcept
    {
        const auto begun = begin_control_packet(destination, busy_service, busy_body_size, value.device_state);
        if (!begun.has_value())
            return std::unexpected(begun.error());
        encode_u16_be(destination, frame::communication_header_size + 2u, value.wait_time_ms);
        encode_u16_be(destination, frame::communication_header_size + 4u, value.control_field);
        return {};
    }

    std::expected<busy, std::error_code> decode_busy_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto valid = validate_control_packet(packet, busy_service, busy_body_size);
        if (!valid.has_value())
            return std::unexpected(valid.error());
        return busy {
            .device_state = packet[frame::communication_header_size + 1u],
            .wait_time_ms = decode_u16_be(packet, frame::communication_header_size + 2u),
            .control_field = decode_u16_be(packet, frame::communication_header_size + 4u),
        };
    }

    expected_socket_address_t client::multicast_peer() const noexcept
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
        recent_sent_packet_count_ = 0u;
        next_sent_packet_index_ = 0u;
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

        const auto packet_size = frame::communication_header_size + value.cemi_bytes.size();
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

        record_sent_packet({packet.data(), packet_size});

        co_return expected_void_t {};
    }

    task_returning_expected_void_t client::send_busy(const busy& value) noexcept(false)
    {
        if (!started_)
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        std::array<std::uint8_t, frame::communication_header_size + busy_body_size> packet {};
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

        record_sent_packet(packet);

        co_return expected_void_t {};
    }

    task_returning_expected_void_t client::send_lost_message(const lost_message& value) noexcept(false)
    {
        if (!started_)
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        std::array<std::uint8_t, frame::communication_header_size + lost_message_body_size> packet {};
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

        record_sent_packet(packet);

        co_return expected_void_t {};
    }

    event_result_t client::to_indication(const cspan_uint8_t packet) noexcept
    {
        const auto decoded = decode_indication_packet(packet);
        if (!decoded.has_value())
            return std::unexpected(decoded.error());
        // Checked here rather than left to the decoder that already ran. Its length arithmetic cannot
        // admit a longer message, but this is the bound that keeps the copy inside cemi_bytes_storage,
        // so it is stated where the copy is.
        if (decoded->cemi_bytes.size() > frame::cemi_max_size)
            return std::unexpected(make_error_code(error::invalid_length));

        received_indication value {};
        value.cemi = decoded->cemi;
        value.cemi_bytes.size = static_cast<std::uint16_t>(decoded->cemi_bytes.size());
        std::copy_n(decoded->cemi_bytes.begin(), decoded->cemi_bytes.size(), value.cemi_bytes.bytes.begin());
        return event {std::move(value)};
    }

    event_result_t client::to_event(const std::uint16_t service, const cspan_uint8_t packet) noexcept
    {
        switch (service)
        {
            case indication_service:
                return to_indication(packet);
            case busy_service:
            {
                const auto decoded = decode_busy_packet(packet);
                if (!decoded.has_value())
                    return std::unexpected(decoded.error());
                note_busy(decoded->wait_time_ms);
                return event {decoded.value()};
            }
            case lost_message_service:
            {
                const auto decoded = decode_lost_message_packet(packet);
                if (!decoded.has_value())
                    return std::unexpected(decoded.error());
                counters_.lost_messages += decoded->count;
                return event {decoded.value()};
            }
            default:
                return std::unexpected(make_error_code(error::unsupported_service));
        }
    }

    bool client::is_own_reflection(const cspan_uint8_t packet) const noexcept
    {
        return std::ranges::any_of(std::span {recent_sent_packets_}.first(recent_sent_packet_count_),
                                   [packet](const sent_packet& sent) noexcept
                                   {
                                       return (packet.size() == sent.size) &&
                                              std::equal(packet.begin(), packet.end(), sent.bytes.begin());
                                   });
    }

    void client::record_sent_packet(const cspan_uint8_t packet) noexcept
    {
        auto& destination = recent_sent_packets_[next_sent_packet_index_];
        std::copy(packet.begin(), packet.end(), destination.bytes.begin());
        destination.size = packet.size();
        next_sent_packet_index_ = (next_sent_packet_index_ + 1u) % recent_sent_packets_.size();
        recent_sent_packet_count_ = std::min(recent_sent_packet_count_ + 1u, recent_sent_packets_.size());
    }

    event_task_t client::receive_event() noexcept(false)
    {
        if (!started_)
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        for (;;)
        {
            transport_peer peer {};
            auto* bytes = reinterpret_cast<std::byte*>(receive_buffer_.data());
            const auto received = co_await transport_.receive(span_byte_t {bytes, receive_buffer_.size()}, peer);
            if (!received.has_value())
                co_return std::unexpected(received.error());
            if (!valid_source_peer(peer))
                co_return std::unexpected(make_error_code(error::connection_failed));
            if ((received.value() == 0u) || (received.value() > receive_buffer_.size()))
                co_return std::unexpected(make_error_code(error::invalid_length));

            const cspan_uint8_t packet {receive_buffer_.data(), received.value()};
            if (is_own_reflection(packet))
            {
                note_reflected();
                continue;
            }

            const auto header = frame::decode_communication_header(packet);
            if (!header.has_value())
                co_return std::unexpected(header.error());

            co_return to_event(header->service_type, packet);
        }
    }


    received_indication_task_t client::receive_indication() noexcept(false)
    {
        for (;;)
        {
            auto received = co_await receive_event();
            if (!received.has_value())
                co_return std::unexpected(received.error());
            if (auto* indication = std::get_if<received_indication>(&received.value()); indication != nullptr)
                co_return std::move(*indication);
        }
    }
}
