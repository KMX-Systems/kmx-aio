/// @file api/kmx/aio/knx/secure/credentials.hpp
/// @brief What an endpoint needs to join a KNX IP Secure profile: tunnelling credentials and routing configuration.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// Both hold derived keys only, never the passwords they came from, so nothing on a network path runs a
/// password hash (P4). @ref kmx::aio::knx::keyring::credentials_for and
/// @ref kmx::aio::knx::keyring::routing_configuration_for build them from an ETS keyring; an application whose
/// keys come from elsewhere fills them in directly.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/ipv4.hpp>
        #include <kmx/aio/knx/secure/common.hpp>
        #include <kmx/aio/knx/secure/key.hpp>

        #include <cstdint>
        #include <expected>
        #include <system_error>
    #endif

namespace kmx::aio::knx::secure
{
    /// @brief What a client needs to open a KNX IP Secure tunnelling session.
    /// @note Move-only, because its keys are.
    struct tunnelling_credentials
    {
        /// @brief The user id SESSION_AUTHENTICATE names.
        std::uint8_t user_id {};
        /// @brief The key derived from the tunnel's user password.
        secret_key user_password_key {};
        /// @brief The key derived from the interface's device authentication code; it verifies SESSION_RESPONSE.
        secret_key device_authentication_code {};
        /// @brief Skips SESSION_RESPONSE verification.
        /// @warning A named opt-out, off by default: without verification an active attacker can stand in for
        ///          the interface.
        bool skip_device_authentication {};
        /// @brief This client's KNX serial number; required and non-zero (P8).
        serial_number_t serial_number {};
    };

    /// @brief Tunnelling credentials, or why they could not be built.
    using tunnelling_credentials_result_t = std::expected<tunnelling_credentials, std::error_code>;

    /// @brief What a router needs to join a KNX IP Secure routing backbone.
    struct routing_configuration
    {
        /// @brief The key every router on the backbone shares.
        secret_key backbone_key {};
        /// @brief The routing multicast group.
        ipv4::storage_t multicast_address {224u, 0u, 23u, 12u};
        /// @brief How far a frame's timer may lag the local timer and still be accepted, in milliseconds.
        std::uint16_t latency_tolerance_ms = 1000u;
        /// @brief This router's KNX serial number; required, non-zero and unique in the installation (P8).
        serial_number_t serial_number {};
        /// @brief How many recently accepted frames are remembered, to drop exact repeats.
        std::uint16_t duplicate_cache_entries = 256u;
    };

    /// @brief A routing configuration, or why it could not be built.
    using routing_configuration_result_t = std::expected<routing_configuration, std::error_code>;
}
#endif // KMX_AIO_FEATURE_KNX
