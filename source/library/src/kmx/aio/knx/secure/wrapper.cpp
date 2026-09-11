/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/wrapper.hpp>

#include <kmx/aio/knx/error.hpp>
#include <kmx/aio/knx/secure/detail/ccm.hpp>
#include <kmx/aio/knx/secure/detail/wrapper_crypto.hpp>

#include <algorithm>
#include <array>

namespace kmx::aio::knx::secure
{
    /// @brief Services that may not travel inside a wrapper: a wrapper itself, and REMOTE_DIAG_REQUEST,
    ///        REMOTE_DIAG_RESPONSE, REMOTE_CONFIG_REQUEST and REMOTE_RESET_REQUEST (§4.3).
    static constexpr std::array<std::uint16_t, 5u> services_refused_inside_wrapper {secure_wrapper_service, 0x0740u, 0x0741u, 0x0742u,
                                                                                    0x0743u};
    /// @brief The only KNXnet/IP protocol version.
    static constexpr std::uint8_t protocol_version = 0x10u;

    [[nodiscard]] static std::unexpected<std::error_code> refuse(const error reason) noexcept
    {
        return std::unexpected(make_error_code(reason));
    }

    /// @brief Writes a wrapper's KNXnet/IP header and security header; @p destination holds @p total_length octets.
    static void write_wrapper_prefix(const span_uint8_t destination, const wrapper_fields& fields, const std::size_t total_length) noexcept
    {
        // Cannot fail: every caller has already checked the destination against total_length.
        (void) frame::encode_communication_header(destination, secure_wrapper_service, static_cast<std::uint16_t>(total_length));
        const auto body = destination.subspan(frame::communication_header_size, wrapper_security_header_size);
        body[0u] = static_cast<std::uint8_t>(fields.session_id >> 8u);
        body[1u] = static_cast<std::uint8_t>(fields.session_id & 0xFFu);
        std::ranges::copy(fields.sequence, body.begin() + 2u);
        std::ranges::copy(fields.serial_number, body.begin() + 8u);
        std::ranges::copy(fields.message_tag, body.begin() + 14u);
    }

    secure_wrapper_result_t decode_secure_wrapper_packet(const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if (header->service_type != secure_wrapper_service)
            return refuse(error::unsupported_service);
        if ((header->total_length != packet.size()) || (packet.size() < (wrapper_overhead + min_wrapped_frame_size)))
            return refuse(error::malformed_frame);

        const auto body = packet.subspan(frame::communication_header_size);
        secure_wrapper_frame value {};
        value.session_id = static_cast<std::uint16_t>((body[0u] << 8u) | body[1u]);
        std::copy_n(body.begin() + 2u, value.sequence.size(), value.sequence.begin());
        std::copy_n(body.begin() + 8u, value.serial_number.size(), value.serial_number.begin());
        std::copy_n(body.begin() + 14u, value.message_tag.size(), value.message_tag.begin());
        value.encrypted_frame = body.subspan(wrapper_security_header_size, body.size() - wrapper_security_header_size - mac_size);
        std::copy_n(body.end() - mac_size, mac_size, value.mac.begin());
        return value;
    }

    expected_void_t encode_secure_wrapper_packet(const span_uint8_t destination, const secure_wrapper_frame& value) noexcept
    {
        const auto total_length = wrapper_overhead + value.encrypted_frame.size();
        if (value.encrypted_frame.size() < min_wrapped_frame_size)
            return refuse(error::malformed_frame);
        if ((total_length > frame::max_frame_size) || (destination.size() < total_length))
            return refuse(error::invalid_length);

        write_wrapper_prefix(destination, wrapper_fields {value.session_id, value.sequence, value.serial_number, value.message_tag},
                             total_length);
        std::ranges::copy(value.encrypted_frame, destination.begin() + frame::communication_header_size + wrapper_security_header_size);
        std::ranges::copy(value.mac, destination.begin() + (total_length - mac_size));
        return {};
    }

    communication_header_result_t check_wrapped_frame(const cspan_uint8_t plain_frame) noexcept
    {
        const auto header = frame::decode_communication_header(plain_frame);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->protocol_version != protocol_version) || (header->total_length != plain_frame.size()))
            return refuse(error::malformed_frame);
        if (std::ranges::find(services_refused_inside_wrapper, header->service_type) != services_refused_inside_wrapper.end())
            return refuse(error::unsupported_service);
        return *header;
    }

    expected_size_t seal_wrapper(const span_uint8_t destination, const secret_key& key, const wrapper_fields& fields,
                                 const cspan_uint8_t plain_frame) noexcept
    {
        return detail::basic_seal_wrapper(detail::evp_backend(), destination, key, fields, plain_frame);
    }

    expected_size_t open_wrapper(const span_uint8_t destination, const secret_key& key, const secure_wrapper_frame& value) noexcept
    {
        return detail::basic_open_wrapper(detail::evp_backend(), destination, key, value);
    }

    namespace detail
    {
        expected_size_t basic_seal_wrapper(const crypto_backend& backend, const span_uint8_t destination, const secret_key& key,
                                           const wrapper_fields& fields, const cspan_uint8_t plain_frame) noexcept
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
            const auto mac = seal(backend, key, wrapper_block_0(fields.sequence, fields.serial_number, fields.message_tag, length),
                                  wrapper_counter_0(fields.sequence, fields.serial_number, fields.message_tag),
                                  destination.first(frame::communication_header_size + 2u), payload);
            if (!mac.has_value())
            {
                cleanse(destination.first(total_length));
                return std::unexpected(mac.error());
            }
            std::ranges::copy(*mac, destination.begin() + (total_length - mac_size));
            return total_length;
        }

        expected_size_t basic_open_wrapper(const crypto_backend& backend, const span_uint8_t destination, const secret_key& key,
                                           const secure_wrapper_frame& value) noexcept
        {
            const auto size = value.encrypted_frame.size();
            if ((destination.size() < size) || ((wrapper_overhead + size) > frame::max_frame_size))
                return refuse(error::invalid_length);

            std::array<std::uint8_t, frame::communication_header_size + wrapper_security_header_size> prefix {};
            write_wrapper_prefix(prefix, wrapper_fields {value.session_id, value.sequence, value.serial_number, value.message_tag},
                                 wrapper_overhead + size);
            const auto payload = destination.first(size);
            std::ranges::copy(value.encrypted_frame, payload.begin());
            const auto opened = open(
                backend, key, wrapper_block_0(value.sequence, value.serial_number, value.message_tag, static_cast<std::uint16_t>(size)),
                wrapper_counter_0(value.sequence, value.serial_number, value.message_tag),
                cspan_uint8_t {prefix}.first(frame::communication_header_size + 2u), payload, value.mac);
            if (!opened.has_value())
            {
                cleanse(payload);
                return std::unexpected(opened.error());
            }
            return size;
        }
    }
}
