/// @file src/kmx/aio/knx/secure/session.cpp
/// @brief The compiled body of the KNX IP Secure session codecs and handshake MACs.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/session.hpp>
#ifndef PCH
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/secure/detail/ccm.hpp>
    #include <kmx/aio/knx/secure/detail/session_crypto.hpp>

    #include <algorithm>
#endif

namespace kmx::aio::knx::secure
{
    /// @brief The host protocols an HPAI may name: UDP and TCP.
    [[nodiscard]] static constexpr bool known_host_protocol(const std::uint8_t protocol) noexcept
    {
        return (protocol == 0x01u) || (protocol == 0x02u);
    }

    [[nodiscard]] static std::unexpected<std::error_code> refuse(const error reason) noexcept
    {
        return std::unexpected(make_error_code(reason));
    }

    /// @brief Checks a datagram announces @p service, in exactly @p size octets.
    [[nodiscard]] static expected_void_t check_header(const cspan_uint8_t packet, const std::uint16_t service,
                                                      const std::size_t size) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if (header->service_type != service)
            return refuse(error::unsupported_service);
        if ((header->protocol_version != 0x10u) || (header->total_length != size) || (packet.size() != size))
            return refuse(error::malformed_frame);
        return {};
    }

    /// @brief Writes the header of a @p size octet datagram for @p service, once @p destination is known to hold it.
    [[nodiscard]] static expected_void_t begin_frame(const span_uint8_t destination, const std::uint16_t service,
                                                     const std::size_t size) noexcept
    {
        if (destination.size() < size)
            return refuse(error::invalid_length);
        if (const auto header = frame::encode_communication_header(destination, service, static_cast<std::uint16_t>(size));
            !header.has_value())
            return std::unexpected(header.error());
        return {};
    }

    expected_void_t encode_session_request_packet(const span_uint8_t destination, const session_request_frame& value) noexcept
    {
        if (!known_host_protocol(value.control_endpoint.protocol))
            return refuse(error::unsupported_hpai);
        if (const auto begun = begin_frame(destination, session_request_service, session_request_size); !begun.has_value())
            return begun;

        const auto& endpoint = value.control_endpoint.endpoint;
        destination[6u] = static_cast<std::uint8_t>(connection::hpai_size);
        destination[7u] = value.control_endpoint.protocol;
        std::ranges::copy(endpoint.address, destination.begin() + 8u);
        destination[12u] = static_cast<std::uint8_t>(endpoint.port >> 8u);
        destination[13u] = static_cast<std::uint8_t>(endpoint.port & 0xFFu);
        std::ranges::copy(value.client_public_key, destination.begin() + 14u);
        return {};
    }

    session_request_result_t decode_session_request_packet(const cspan_uint8_t packet) noexcept
    {
        if (const auto checked = check_header(packet, session_request_service, session_request_size); !checked.has_value())
            return std::unexpected(checked.error());
        if (packet[6u] != connection::hpai_size)
            return refuse(error::malformed_frame);
        if (!known_host_protocol(packet[7u]))
            return refuse(error::unsupported_hpai);

        session_request_frame value {};
        value.control_endpoint.protocol = packet[7u];
        std::copy_n(packet.begin() + 8u, value.control_endpoint.endpoint.address.size(), value.control_endpoint.endpoint.address.begin());
        value.control_endpoint.endpoint.port = static_cast<std::uint16_t>((packet[12u] << 8u) | packet[13u]);
        std::copy_n(packet.begin() + 14u, x25519_key_size, value.client_public_key.begin());
        return value;
    }

    expected_void_t encode_session_response_packet(const span_uint8_t destination, const session_response_frame& value) noexcept
    {
        if (const auto begun = begin_frame(destination, session_response_service, session_response_size); !begun.has_value())
            return begun;

        destination[6u] = static_cast<std::uint8_t>(value.session_id >> 8u);
        destination[7u] = static_cast<std::uint8_t>(value.session_id & 0xFFu);
        std::ranges::copy(value.server_public_key, destination.begin() + 8u);
        std::ranges::copy(value.mac, destination.begin() + 8u + x25519_key_size);
        return {};
    }

    session_response_result_t decode_session_response_packet(const cspan_uint8_t packet) noexcept
    {
        if (const auto checked = check_header(packet, session_response_service, session_response_size); !checked.has_value())
            return std::unexpected(checked.error());

        session_response_frame value {};
        value.session_id = static_cast<std::uint16_t>((packet[6u] << 8u) | packet[7u]);
        std::copy_n(packet.begin() + 8u, x25519_key_size, value.server_public_key.begin());
        std::copy_n(packet.begin() + 8u + x25519_key_size, mac_size, value.mac.begin());
        return value;
    }

    expected_void_t encode_session_authenticate_packet(const span_uint8_t destination, const session_authenticate_frame& value) noexcept
    {
        if (const auto begun = begin_frame(destination, session_authenticate_service, session_authenticate_size); !begun.has_value())
            return begun;

        destination[6u] = 0x00u;
        destination[7u] = value.user_id;
        std::ranges::copy(value.mac, destination.begin() + 8u);
        return {};
    }

    session_authenticate_result_t decode_session_authenticate_packet(const cspan_uint8_t packet) noexcept
    {
        if (const auto checked = check_header(packet, session_authenticate_service, session_authenticate_size); !checked.has_value())
            return std::unexpected(checked.error());
        if (packet[6u] != 0x00u)
            return refuse(error::malformed_frame);

        session_authenticate_frame value {.user_id = packet[7u]};
        std::copy_n(packet.begin() + 8u, mac_size, value.mac.begin());
        return value;
    }

    expected_void_t encode_session_status_packet(const span_uint8_t destination, const session_status_frame& value) noexcept
    {
        if (static_cast<std::uint8_t>(value.status) > static_cast<std::uint8_t>(session_status::close))
            return refuse(error::invalid_configuration);
        if (const auto begun = begin_frame(destination, session_status_service, session_status_size); !begun.has_value())
            return begun;

        destination[6u] = static_cast<std::uint8_t>(value.status);
        destination[7u] = 0x00u;
        return {};
    }

    session_status_result_t decode_session_status_packet(const cspan_uint8_t packet) noexcept
    {
        if (const auto checked = check_header(packet, session_status_service, session_status_size); !checked.has_value())
            return std::unexpected(checked.error());
        if (packet[6u] > static_cast<std::uint8_t>(session_status::close))
            return refuse(error::malformed_frame);
        return session_status_frame {static_cast<session_status>(packet[6u])};
    }

    session_mac_result_t session_response_mac(const secret_key& device_authentication_code, const std::uint16_t session_id,
                                              const x25519_public_key_t& client_public_key,
                                              const x25519_public_key_t& server_public_key) noexcept
    {
        return detail::basic_session_response_mac({detail::evp_backend(), device_authentication_code}, session_id, client_public_key,
                                                  server_public_key);
    }

    expected_void_t verify_session_response(const secret_key& device_authentication_code, const session_response_frame& response,
                                            const x25519_public_key_t& client_public_key) noexcept
    {
        const auto expected =
            session_response_mac(device_authentication_code, response.session_id, client_public_key, response.server_public_key);
        if (!expected.has_value())
            return std::unexpected(expected.error());
        if (!detail::constant_time_equal(*expected, response.mac))
            return refuse(error::secure_authentication_failed);
        return {};
    }

    session_mac_result_t session_authenticate_mac(const secret_key& user_password_key, const std::uint8_t user_id,
                                                  const x25519_public_key_t& client_public_key,
                                                  const x25519_public_key_t& server_public_key) noexcept
    {
        return detail::basic_session_authenticate_mac({detail::evp_backend(), user_password_key}, user_id, client_public_key,
                                                      server_public_key);
    }

    expected_void_t verify_session_authenticate(const secret_key& user_password_key, const session_authenticate_frame& value,
                                                const x25519_public_key_t& client_public_key,
                                                const x25519_public_key_t& server_public_key) noexcept
    {
        const auto expected = session_authenticate_mac(user_password_key, value.user_id, client_public_key, server_public_key);
        if (!expected.has_value())
            return std::unexpected(expected.error());
        if (!detail::constant_time_equal(*expected, value.mac))
            return refuse(error::secure_authentication_failed);
        return {};
    }

    secret_key_result_t derive_session_key(const x25519_private_key& private_key, const x25519_public_key_t& peer_public_key) noexcept
    {
        return detail::derive_session_key(detail::evp_backend(), private_key, peer_public_key);
    }
}
