/// @file kmx/aio/knx/secure/server_configuration.hpp
/// @brief What a KNX IP Secure tunnelling server needs: its device authentication code, its users, and its limits.
/// @details
/// Handed to @ref kmx::aio::knx::server_config::secure. Users are held as derived keys only (P4), each with the tunnel
/// addresses it may be given. @ref kmx::aio::knx::keyring::server_configuration_for builds one from an ETS keyring.
/// @reference KNX System Specifications, 03/08/09 "KNXnet/IP Security".
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <cstdint>
        #include <expected>
        #include <system_error>
        #include <vector>
    #endif

    #include <kmx/aio/knx/address.hpp>
    #include <kmx/aio/knx/secure/common.hpp>
    #include <kmx/aio/knx/secure/key.hpp>

namespace kmx::aio::knx::secure
{
    /// @brief The most sessions a secure server holds at once, whatever its configuration asks for.
    inline constexpr std::uint16_t max_server_sessions = 255u;

    /// @brief A user a secure tunnelling server lets in, and the tunnel addresses it may tunnel under.
    /// @note Move-only, because its key is.
    struct tunnelling_user
    {
        /// @brief The user id SESSION_AUTHENTICATE names; 2 to 127 for tunnelling users.
        std::uint8_t user_id {};
        /// @brief The key derived from the user's password (P4).
        secret_key password_key {};
        /// @brief The individual addresses this user may be given; a CONNECT_REQUEST for any other is refused.
        std::vector<individual_address> tunnel_addresses {};
    };

    /// @brief What a KNX IP Secure tunnelling server needs.
    /// @note Move-only, because its keys are. A server holds it through `std::shared_ptr<const server_configuration>`,
    ///       which keeps the server configuration it is part of copyable and its keys in one place.
    struct server_configuration
    {
        /// @brief The key derived from the device authentication code; it authenticates every SESSION_RESPONSE.
        secret_key device_authentication_code {};
        /// @brief The users allowed in.
        std::vector<tunnelling_user> users {};
        /// @brief The server's KNX serial number, which its wrappers carry; required and non-zero (P8).
        serial_number_t serial_number {};
        /// @brief How many sessions may be open at once; @ref max_server_sessions at most.
        std::uint16_t max_sessions = 16u;
        /// @brief How many sessions one peer address may hold unauthenticated at once.
        std::uint8_t max_unauthenticated_per_peer = 2u;
        /// @brief How long a session may stay unauthenticated, in milliseconds; also how long a connection may carry
        ///        no session before the server closes it.
        std::uint32_t unauthenticated_lifetime_ms = 10'000u;
    };

    /// @brief A server configuration, or why it could not be built.
    using server_configuration_result_t = std::expected<server_configuration, std::error_code>;
}
#endif // KMX_AIO_FEATURE_KNX
