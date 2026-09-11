/// @file src/kmx/aio/knx/secure/detail/wrapper_crypto.cpp
/// @brief The SECURE_WRAPPER and TIMER_NOTIFY cryptography over an explicit backend, and the headers both put on the wire.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/detail/wrapper_crypto.hpp>
#ifndef PCH
    #include <kmx/aio/knx/error.hpp>

    #include <algorithm>
#endif

namespace kmx::aio::knx::secure::detail
{
    [[nodiscard]] static std::unexpected<std::error_code> refuse(const error reason) noexcept
    {
        return std::unexpected(make_error_code(reason));
    }

    void write_wrapper_prefix(const span_uint8_t destination, const wrapper_fields& fields, const std::size_t total_length) noexcept
    {
        // Cannot fail: every caller has already checked the destination against total_length.
        static_cast<void>(frame::encode_communication_header(destination, wrapper_service, static_cast<std::uint16_t>(total_length)));
        const auto body = destination.subspan(frame::communication_header_size, wrapper_security_header_size);
        body[0u] = static_cast<std::uint8_t>(fields.session_id >> 8u);
        body[1u] = static_cast<std::uint8_t>(fields.session_id & 0xFFu);
        std::ranges::copy(fields.sequence, body.begin() + 2u);
        std::ranges::copy(fields.serial_number, body.begin() + 8u);
        std::ranges::copy(fields.message_tag, body.begin() + 14u);
    }

    timer_notify_header_t timer_notify_header() noexcept
    {
        timer_notify_header_t header {};
        // Cannot fail: the destination is exactly one header long.
        static_cast<void>(frame::encode_communication_header(header, timer_notify_service, static_cast<std::uint16_t>(timer_notify_size)));
        return header;
    }

    expected_size_t basic_seal_wrapper(const cipher& with, const span_uint8_t destination, const wrapper_fields& fields,
                                       const cspan_uint8_t plain_frame) noexcept
    {
        if (const auto checked = check_wrapped_frame(plain_frame); !checked.has_value())
            return std::unexpected(checked.error());
        const auto total_length = wrapper_overhead + plain_frame.size();
        if ((plain_frame.size() > max_wrapped_frame_size) || (destination.size() < total_length))
            return refuse(error::invalid_length);

        write_wrapper_prefix(destination, fields, total_length);
        const auto payload = destination.subspan(frame::communication_header_size + wrapper_security_header_size, plain_frame.size());
        std::ranges::copy(plain_frame, payload.begin());
        const auto length = static_cast<std::uint16_t>(plain_frame.size());
        const auto mac = seal(with, message {.block_0 = wrapper_block_0(fields.sequence, fields.serial_number, fields.message_tag, length),
                                             .counter_0 = wrapper_counter_0(fields.sequence, fields.serial_number, fields.message_tag),
                                             .associated_data = destination.first(frame::communication_header_size + 2u),
                                             .payload = payload});
        if (!mac.has_value())
        {
            cleanse(destination.first(total_length));
            return std::unexpected(mac.error());
        }

        std::ranges::copy(*mac, destination.begin() + (total_length - mac_size));
        return total_length;
    }

    expected_size_t basic_open_wrapper(const cipher& with, const span_uint8_t destination, const wrapper_frame& value) noexcept
    {
        const auto size = value.encrypted_frame.size();
        if ((destination.size() < size) || ((wrapper_overhead + size) > frame::max_total_length))
            return refuse(error::invalid_length);

        std::array<std::uint8_t, frame::communication_header_size + wrapper_security_header_size> prefix {};
        write_wrapper_prefix(prefix, wrapper_fields {value.session_id, value.sequence, value.serial_number, value.message_tag},
                             wrapper_overhead + size);
        const auto payload = destination.first(size);
        std::ranges::copy(value.encrypted_frame, payload.begin());
        const auto opened = open(
            with,
            message {.block_0 = wrapper_block_0(value.sequence, value.serial_number, value.message_tag, static_cast<std::uint16_t>(size)),
                     .counter_0 = wrapper_counter_0(value.sequence, value.serial_number, value.message_tag),
                     .associated_data = cspan_uint8_t {prefix}.first(frame::communication_header_size + 2u),
                     .payload = payload},
            value.mac);
        if (!opened.has_value())
        {
            cleanse(payload);
            return std::unexpected(opened.error());
        }

        return size;
    }

    timer_notify_result_t basic_make_timer_notify(const cipher& with, const std::uint64_t timer_value, const serial_number_t& serial_number,
                                                  const message_tag_t& message_tag) noexcept
    {
        if (timer_value > max_sequence)
            return refuse(error::invalid_configuration);
        timer_notify_frame value {encode_sequence(timer_value), serial_number, message_tag, {}};
        const auto header = timer_notify_header();
        const auto mac = seal(with, message {.block_0 = wrapper_block_0(value.timer_value, serial_number, message_tag, 0u),
                                             .counter_0 = wrapper_counter_0(value.timer_value, serial_number, message_tag),
                                             .associated_data = header});
        if (!mac.has_value())
            return std::unexpected(mac.error());
        value.mac = *mac;
        return value;
    }

    expected_void_t basic_verify_timer_notify(const cipher& with, const timer_notify_frame& value) noexcept
    {
        const auto header = timer_notify_header();
        return open(with,
                    message {.block_0 = wrapper_block_0(value.timer_value, value.serial_number, value.message_tag, 0u),
                             .counter_0 = wrapper_counter_0(value.timer_value, value.serial_number, value.message_tag),
                             .associated_data = header},
                    value.mac);
    }
}
