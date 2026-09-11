/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/timer_notify.hpp>

#include <kmx/aio/knx/error.hpp>
#include <kmx/aio/knx/secure/detail/ccm.hpp>
#include <kmx/aio/knx/secure/detail/wrapper_crypto.hpp>

#include <algorithm>
#include <array>

namespace kmx::aio::knx::secure
{
    /// @brief The KNXnet/IP header of every TIMER_NOTIFY, which is the associated data of its MAC.
    using timer_notify_header_t = std::array<std::uint8_t, frame::communication_header_size>;

    [[nodiscard]] static std::unexpected<std::error_code> refuse(const error reason) noexcept
    {
        return std::unexpected(make_error_code(reason));
    }

    [[nodiscard]] static timer_notify_header_t timer_notify_header() noexcept
    {
        timer_notify_header_t header {};
        // Cannot fail: the destination is exactly one header long.
        (void) frame::encode_communication_header(header, timer_notify_service, static_cast<std::uint16_t>(timer_notify_size));
        return header;
    }

    timer_notify_result_t decode_timer_notify_packet(const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if (header->service_type != timer_notify_service)
            return refuse(error::unsupported_service);
        if ((header->total_length != packet.size()) || (packet.size() != timer_notify_size))
            return refuse(error::malformed_frame);

        const auto body = packet.subspan(frame::communication_header_size);
        timer_notify_frame value {};
        std::ranges::copy(body.first(6u), value.timer_value.begin());
        std::ranges::copy(body.subspan(6u, 6u), value.serial_number.begin());
        std::ranges::copy(body.subspan(12u, 2u), value.message_tag.begin());
        std::ranges::copy(body.subspan(14u, mac_size), value.mac.begin());
        return value;
    }

    expected_void_t encode_timer_notify_packet(const span_uint8_t destination, const timer_notify_frame& value) noexcept
    {
        if (destination.size() < timer_notify_size)
            return refuse(error::invalid_length);
        const auto header = timer_notify_header();
        auto field = std::ranges::copy(header, destination.begin()).out;
        field = std::ranges::copy(value.timer_value, field).out;
        field = std::ranges::copy(value.serial_number, field).out;
        field = std::ranges::copy(value.message_tag, field).out;
        std::ranges::copy(value.mac, field);
        return {};
    }

    timer_notify_result_t make_timer_notify(const secret_key& backbone_key, const std::uint64_t timer_value,
                                            const serial_number_t& serial_number, const message_tag_t& message_tag) noexcept
    {
        return detail::basic_make_timer_notify(detail::evp_backend(), backbone_key, timer_value, serial_number, message_tag);
    }

    expected_void_t verify_timer_notify(const secret_key& backbone_key, const timer_notify_frame& value) noexcept
    {
        return detail::basic_verify_timer_notify(detail::evp_backend(), backbone_key, value);
    }

    namespace detail
    {
        timer_notify_result_t basic_make_timer_notify(const crypto_backend& backend, const secret_key& backbone_key,
                                                      const std::uint64_t timer_value, const serial_number_t& serial_number,
                                                      const message_tag_t& message_tag) noexcept
        {
            if (timer_value > max_sequence)
                return refuse(error::invalid_configuration);
            timer_notify_frame value {encode_sequence(timer_value), serial_number, message_tag, {}};
            const auto header = timer_notify_header();
            const auto mac = seal(backend, backbone_key, wrapper_block_0(value.timer_value, serial_number, message_tag, 0u),
                                  wrapper_counter_0(value.timer_value, serial_number, message_tag), header, span_uint8_t {});
            if (!mac.has_value())
                return std::unexpected(mac.error());
            value.mac = *mac;
            return value;
        }

        expected_void_t basic_verify_timer_notify(const crypto_backend& backend, const secret_key& backbone_key,
                                                  const timer_notify_frame& value) noexcept
        {
            const auto header = timer_notify_header();
            return open(backend, backbone_key, wrapper_block_0(value.timer_value, value.serial_number, value.message_tag, 0u),
                        wrapper_counter_0(value.timer_value, value.serial_number, value.message_tag), header, span_uint8_t {}, value.mac);
        }
    }
}
