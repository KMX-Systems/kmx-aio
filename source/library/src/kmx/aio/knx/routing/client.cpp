/// @file src/kmx/aio/knx/routing/client.cpp
/// @brief The compiled body of the KNXnet/IP routing client, with optional IP Secure.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/routing/client.hpp>
#ifndef PCH
    #include <kmx/aio/async_mutex.hpp>
    #include <kmx/aio/knx/cemi.hpp>
    #include <kmx/aio/knx/data_secure/context.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/routing/detail/secure_state.hpp>
    #include <kmx/aio/knx/secure/entropy.hpp>
    #include <kmx/aio/knx/secure/routing_timer_state.hpp>
    #include <kmx/aio/knx/secure/timer_notify.hpp>
    #include <kmx/aio/knx/secure/wrapper.hpp>

    #include <algorithm>
    #include <chrono>
    #include <limits>
    #include <span>
    #include <utility>
    #include <variant>
    #include <netinet/in.h>
#endif

namespace kmx::aio::knx::routing
{
    /// @brief Indicates whether a service is one routing carries, and so one a routing wrapper may deliver.
    [[nodiscard]] static constexpr bool carried_service(const std::uint16_t service) noexcept
    {
        return (service == indication_service) || (service == busy_service) || (service == lost_message_service);
    }

    client::client(datagram_transport& transport, const multicast_configuration configuration, const clock_now_function clock_now) noexcept:
        transport_(transport),
        configuration_(configuration),
        clock_now_(clock_now)
    {
    }

    client::client(datagram_transport& transport, const multicast_configuration configuration, secure_options options) noexcept(false):
        transport_(transport),
        configuration_(configuration),
        clock_now_(options.clock_now),
        secure_(std::make_unique<detail::secure_state>(std::move(options.settings), options.clock_ms,
                                                       (options.entropy != nullptr) ? *options.entropy : secure::system_entropy()))
    {
    }

    client::~client() noexcept = default;

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
        return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    expected_void_t client::validate_secure() const noexcept
    {
        if (secure_ == nullptr)
            return {};
        if (!secure::valid_serial_number(secure_->configuration.serial_number))
            return std::unexpected(make_error_code(error::invalid_configuration));
        if (secure_->configuration.backbone_key.empty())
            return std::unexpected(make_error_code(error::secure_key_missing));
        return {};
    }

    expected_void_t client::start() noexcept
    {
        if (started_)
            return {};

        const auto valid = validate(configuration_);
        if (!valid.has_value())
            return std::unexpected(make_error_code(valid.error()));
        if (const auto secured = validate_secure(); !secured.has_value())
            return secured;

        const auto joined = transport_.join_multicast_group(configuration_);
        if (!joined.has_value())
            return std::unexpected(joined.error());

        started_ = true;
        if (secure_ != nullptr)
        {
            // Each start owes the group a synchronisation request, which the notify path sends.
            secure_->synchronisation_due = true;
            secure_->publish_state();
        }

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
        busy_until_ms_.store(0u, std::memory_order_relaxed);
        if (secure_ != nullptr)
        {
            secure_->timer.reset();
            secure_->synchronisation_due = false;
            secure_->publish_state();
        }

        return {};
    }

    void client::note_busy(const std::uint32_t backoff_ms) noexcept
    {
        ++counters_.busy_messages;
        counters_.busy_backoff_ms = backoff_ms;
        busy_until_ms_.store(now_ms() + backoff_ms, std::memory_order_relaxed);
    }

    void client::note_lost() noexcept
    {
        ++counters_.lost_messages;
    }

    void client::note_reflected() noexcept
    {
        ++counters_.reflected_messages;
    }

    std::uint64_t client::next_timer_deadline_ms() const noexcept
    {
        if (secure_ == nullptr)
            return std::numeric_limits<std::uint64_t>::max();
        return secure_->next_deadline_ms.load(std::memory_order_relaxed);
    }

    bool client::timer_synchronised() const noexcept
    {
        return (secure_ != nullptr) && secure_->synchronised.load(std::memory_order_relaxed);
    }

    const secure::statistics& client::secure_counters() const noexcept
    {
        static const secure::statistics none {};
        return (secure_ == nullptr) ? none : secure_->timer.counters();
    }

    task_returning_expected_void_t client::send_indication(const indication& value) noexcept(false)
    {
        if (!started_)
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        if (static_cast<std::int32_t>(busy_until_ms_.load(std::memory_order_relaxed) - now_ms()) > 0)
            co_return std::unexpected(make_error_code(error::timeout));

        // Data Secure is the innermost layer: the cEMI is secured before it is framed, and so before any wrapper.
        auto* const context = data_secure_.load();
        byte_buffer_t secured {};
        if (context != nullptr)
        {
            auto result = context->secure_frame(value.cemi_bytes);
            if (!result.has_value())
                co_return std::unexpected(result.error());
            secured = std::move(*result);
        }

        const indication outgoing {.cemi_bytes = (context != nullptr) ? cspan_uint8_t {secured} : value.cemi_bytes};
        std::array<std::uint8_t, frame::max_datagram_size> packet {};
        const auto encoded = encode_indication_packet(packet, outgoing);
        if (!encoded.has_value())
            co_return std::unexpected(encoded.error());
        co_return co_await send_packet(cspan_uint8_t {packet.data(), frame::communication_header_size + outgoing.cemi_bytes.size()});
    }

    task_returning_expected_void_t client::send_busy(const busy& value) noexcept(false)
    {
        if (!started_)
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        std::array<std::uint8_t, frame::communication_header_size + busy_body_size> packet {};
        const auto encoded = encode_busy_packet(packet, value);
        if (!encoded.has_value())
            co_return std::unexpected(encoded.error());
        co_return co_await send_packet(packet);
    }

    task_returning_expected_void_t client::send_lost_message(const lost_message& value) noexcept(false)
    {
        if (!started_)
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        std::array<std::uint8_t, frame::communication_header_size + lost_message_body_size> packet {};
        const auto encoded = encode_lost_message_packet(packet, value);
        if (!encoded.has_value())
            co_return std::unexpected(encoded.error());
        co_return co_await send_packet(packet);
    }

    task_returning_expected_void_t client::send_packet(const cspan_uint8_t packet) noexcept(false)
    {
        const auto destination = multicast_peer();
        if (!destination.has_value())
            co_return std::unexpected(destination.error());

        std::array<std::uint8_t, frame::max_datagram_size> wrapped {};
        auto outgoing = packet;
        if (secure_ != nullptr)
        {
            const auto sealed = co_await seal_outgoing(wrapped, packet);
            if (!sealed.has_value())
                co_return std::unexpected(sealed.error());
            outgoing = cspan_uint8_t {wrapped.data(), *sealed};
        }

        const auto* bytes = reinterpret_cast<const std::byte*>(outgoing.data());
        const auto sent = co_await transport_.send(cspan_byte_t {bytes, outgoing.size()},
                                                   reinterpret_cast<const sockaddr*>(&destination->storage), destination->length);
        if (!sent.has_value())
            co_return std::unexpected(sent.error());
        if (*sent != outgoing.size())
            co_return std::unexpected(make_error_code(error::connection_failed));
        // A secure send was recorded before it left; see seal_outgoing.
        if (secure_ == nullptr)
            record_sent_packet(outgoing);
        co_return expected_void_t {};
    }

    task_returning_expected_size_t client::seal_outgoing(const span_uint8_t destination, const cspan_uint8_t packet) noexcept(false)
    {
        auto& state = *secure_;
        const auto guard = co_await state.mutex.lock();
        secure::message_tag_t tag {};
        if (const auto drawn = state.entropy.fill(tag); !drawn.has_value())
            co_return std::unexpected(drawn.error());

        const auto timer_value = state.timer.on_outgoing_wrapper(state.now_ms());
        const secure::wrapper_fields fields {0u, secure::encode_sequence(timer_value), state.configuration.serial_number, tag};
        const auto sealed = secure::seal_wrapper(destination, state.configuration.backbone_key, fields, packet);
        if (sealed.has_value())
        {
            // Recorded before the datagram leaves, so a copy the group reflects back straight away is recognised.
            record_sent_packet(destination.first(*sealed));
            state.timer.remember_sent({timer_value, fields.serial_number, tag});
        }

        state.publish_state();
        co_return sealed;
    }

    task_returning_expected_void_t client::notify_timer() noexcept(false)
    {
        if (!started_ || (secure_ == nullptr))
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        const auto destination = multicast_peer();
        if (!destination.has_value())
            co_return std::unexpected(destination.error());

        std::array<std::uint8_t, secure::timer_notify_size> packet {};
        const auto prepared = co_await prepare_timer_notify(packet);
        if (!prepared.has_value())
            co_return std::unexpected(prepared.error());
        if (!*prepared)
            co_return expected_void_t {};

        const auto* bytes = reinterpret_cast<const std::byte*>(packet.data());
        const auto sent = co_await transport_.send(cspan_byte_t {bytes, packet.size()},
                                                   reinterpret_cast<const sockaddr*>(&destination->storage), destination->length);
        if (!sent.has_value())
            co_return std::unexpected(sent.error());
        if (*sent != packet.size())
            co_return std::unexpected(make_error_code(error::connection_failed));
        co_return expected_void_t {};
    }

    task<client::prepared_notify_t> client::prepare_timer_notify(const span_uint8_t packet) noexcept(false)
    {
        auto& state = *secure_;
        const auto guard = co_await state.mutex.lock();
        const auto now = state.now_ms();
        std::optional<secure::timer_notify_request> request {};
        if (state.synchronisation_due)
            request = state.timer.begin_synchronisation(now);
        else
            request = state.timer.take_due_notify(now);
        state.synchronisation_due = false;
        state.publish_state();
        if (!request.has_value())
            co_return false;

        const auto notify = secure::make_timer_notify(state.configuration.backbone_key, state.timer.timer_value(now),
                                                      request->serial_number, request->message_tag);
        if (!notify.has_value())
            co_return std::unexpected(notify.error());
        if (const auto encoded = secure::encode_timer_notify_packet(packet, *notify); !encoded.has_value())
            co_return std::unexpected(encoded.error());
        record_sent_packet(packet);
        co_return true;
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
        return std::ranges::any_of(
            std::span {recent_sent_packets_}.first(recent_sent_packet_count_), [packet](const sent_packet& sent) noexcept
            { return (packet.size() == sent.size) && std::equal(packet.begin(), packet.end(), sent.bytes.begin()); });
    }

    void client::record_sent_packet(const cspan_uint8_t packet) noexcept
    {
        auto& destination = recent_sent_packets_[next_sent_packet_index_];
        std::copy(packet.begin(), packet.end(), destination.bytes.begin());
        destination.size = packet.size();
        next_sent_packet_index_ = (next_sent_packet_index_ + 1u) % recent_sent_packets_.size();
        recent_sent_packet_count_ = std::min(recent_sent_packet_count_ + 1u, recent_sent_packets_.size());
    }

    client::optional_event_t client::plain_event(const cspan_uint8_t packet) noexcept
    {
        if (is_own_reflection(packet))
        {
            note_reflected();
            return std::nullopt;
        }

        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return event_result_t {std::unexpected(header.error())};
        return to_event(header->service_type, packet);
    }

    task<client::optional_event_t> client::secure_event(const cspan_uint8_t packet) noexcept(false)
    {
        auto& state = *secure_;
        const auto guard = co_await state.mutex.lock();
        optional_event_t outcome {};
        const auto header = frame::decode_communication_header(packet);
        if (is_own_reflection(packet))
            note_reflected();
        else if (!header.has_value())
            outcome = event_result_t {std::unexpected(header.error())};
        else
            outcome = secured_event(header->service_type, packet);
        state.publish_state();
        co_return outcome;
    }

    client::optional_event_t client::secured_event(const std::uint16_t service, const cspan_uint8_t packet) noexcept
    {
        switch (service)
        {
            case secure::wrapper_service:
                return unwrap_event(packet);
            case secure::timer_notify_service:
                apply_timer_notify(packet);
                return std::nullopt;
            case indication_service:
            case busy_service:
            case lost_message_service:
                // P1: with security configured, routing traffic that is not wrapped is refused, not delivered.
                secure_->timer.note_refused(secure::refusal::unencrypted);
                return std::nullopt;
            default:
                return to_event(service, packet);
        }
    }

    client::optional_event_t client::unwrap_event(const cspan_uint8_t packet) noexcept
    {
        auto& state = *secure_;
        const auto wrapper = secure::decode_wrapper_packet(packet);
        const auto opened = (wrapper.has_value() && (wrapper->session_id == 0u)) ?
                                secure::open_wrapper(state.plain, state.configuration.backbone_key, *wrapper) :
                                expected_size_t {std::unexpected(make_error_code(error::secure_authentication_failed))};
        if (!opened.has_value())
        {
            state.timer.note_refused(secure::refusal::authentication);
            return std::nullopt;
        }

        // Only now, with the MAC verified, may the frame reach the service check and then the timer (P2).
        const cspan_uint8_t plain {state.plain.data(), *opened};
        const auto inner = secure::check_wrapped_frame(plain);
        if (!inner.has_value() || !carried_service(inner->service_type))
        {
            state.timer.note_refused(secure::refusal::service);
            return std::nullopt;
        }

        const secure::routing_frame_identity identity {secure::decode_sequence(wrapper->sequence), wrapper->serial_number,
                                                       wrapper->message_tag};
        if (state.timer.on_wrapper(state.now_ms(), identity) != secure::wrapper_verdict::accepted)
            return std::nullopt;
        return to_event(inner->service_type, plain);
    }

    void client::apply_timer_notify(const cspan_uint8_t packet) noexcept
    {
        auto& state = *secure_;
        const auto notify = secure::decode_timer_notify_packet(packet);
        if (!notify.has_value() || !secure::verify_timer_notify(state.configuration.backbone_key, *notify).has_value())
        {
            state.timer.note_refused(secure::refusal::authentication);
            return;
        }

        state.timer.on_timer_notify(state.now_ms(),
                                    {secure::decode_sequence(notify->timer_value), notify->serial_number, notify->message_tag});
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
            optional_event_t outcome {};
            if (secure_ == nullptr)
                outcome = plain_event(packet);
            else
                outcome = co_await secure_event(packet);
            if (outcome.has_value() && open_data_secure(*outcome))
                co_return std::move(*outcome);
        }
    }

    bool client::open_data_secure(event_result_t& outcome) const noexcept(false)
    {
        auto* const context = data_secure_.load();
        auto* const received = ((context != nullptr) && outcome.has_value()) ? std::get_if<received_indication>(&outcome.value()) : nullptr;
        if (received == nullptr)
            return true;
        // A telegram Data Secure refuses has been counted there, and is read past.
        const auto opened = context->open_frame(received->cemi_bytes.span());
        if (!opened.has_value() || (opened->size() > received->cemi_bytes.bytes.size()))
            return false;
        const auto frame = cemi::decode(*opened);
        if (!frame.has_value())
            return false;
        received->cemi = *frame;
        received->cemi_bytes.size = static_cast<std::uint16_t>(opened->size());
        std::ranges::copy(*opened, received->cemi_bytes.bytes.begin());
        return true;
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
