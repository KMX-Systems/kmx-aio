/// @file src/kmx/aio/knx/secure/timer_notify.cpp
/// @brief TIMER_NOTIFY encoding and decoding, and its MAC generation and verification for KNX IP Secure routing.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/timer_notify.hpp>
#ifndef PCH
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/secure/detail/ccm.hpp>
    #include <kmx/aio/knx/secure/detail/wrapper_crypto.hpp>

    #include <algorithm>
#endif

namespace kmx::aio::knx::secure
{
    [[nodiscard]] static std::unexpected<std::error_code> refuse(const error reason) noexcept
    {
        return std::unexpected(make_error_code(reason));
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
        const auto header = detail::timer_notify_header();
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
        return detail::basic_make_timer_notify({detail::evp_backend(), backbone_key}, timer_value, serial_number, message_tag);
    }

    expected_void_t verify_timer_notify(const secret_key& backbone_key, const timer_notify_frame& value) noexcept
    {
        return detail::basic_verify_timer_notify({detail::evp_backend(), backbone_key}, value);
    }
}
