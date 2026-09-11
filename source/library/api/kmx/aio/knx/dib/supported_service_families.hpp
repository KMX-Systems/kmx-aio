/// @file api/kmx/aio/knx/dib/supported_service_families.hpp
/// @brief The KNXnet/IP service families, and the description information block that lists those a server serves.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @reference KNX System Specifications, 03/08/02 "Core", description information block.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <cstdint>
        #include <vector>
    #endif

namespace kmx::aio::knx::dib
{
    /// @brief The KNXnet/IP service families a server can serve.
    /// @details The value is also the high octet of every service type in that family, which is why
    ///          @ref family_of can name the family a service belongs to without a table.
    enum class service_family : std::uint8_t
    {
        /// @brief Discovery, description and connection management.
        core = 0x02u,
        /// @brief Device management: reading and writing the server's own interface objects.
        device_management = 0x03u,
        /// @brief Tunnelling onto the bus.
        tunnelling = 0x04u,
        /// @brief Multicast routing.
        routing = 0x05u,
        /// @brief Remote logging.
        remote_logging = 0x06u,
        /// @brief Remote configuration and diagnosis.
        remote_configuration = 0x07u,
        /// @brief Object server.
        object_server = 0x08u,
        /// @brief KNX Secure.
        security = 0x09u,
    };

    /// @brief One service family and the version of it a server serves.
    struct service_family_entry
    {
        /// @brief Which family.
        service_family family = service_family::core;
        /// @brief The version of it the server serves.
        std::uint8_t version = 1u;

        /// @brief Compares two entries by family and version.
        [[nodiscard]] friend constexpr bool operator==(const service_family_entry&, const service_family_entry&) noexcept = default;
    };

    /// @brief A SUPP_SVC_FAMILIES or SECURED_SERVICE_FAMILIES block.
    struct supported_service_families
    {
        /// @brief Whether this is the secured variant, which names the families that require KNX Secure.
        bool secured {};
        /// @brief The families served, each with its version.
        std::vector<service_family_entry> families {};

        /// @brief Indicates whether a family is present, at any version.
        [[nodiscard]] bool contains(const service_family value) const noexcept;
        /// @brief Indicates whether a family is present at or above a version.
        [[nodiscard]] bool contains(const service_family value, const std::uint8_t minimum_version) const noexcept;
    };
}
#endif // KMX_AIO_FEATURE_KNX
