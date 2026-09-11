/// @file src/kmx/aio/knx/dib/supported_service_families.cpp
/// @brief Service family lookups on a KNXnet/IP SUPP_SVC_FAMILIES or SECURED_SERVICE_FAMILIES block.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/dib/supported_service_families.hpp>
#ifndef PCH
    #include <algorithm>
    #include <cstdint>
#endif

namespace kmx::aio::knx::dib
{
    bool supported_service_families::contains(const service_family value) const noexcept
    {
        return std::ranges::any_of(families, [value](const auto& entry) noexcept { return entry.family == value; });
    }

    bool supported_service_families::contains(const service_family value, const std::uint8_t minimum_version) const noexcept
    {
        return std::ranges::any_of(families, [value, minimum_version](const auto& entry) noexcept
                                   { return (entry.family == value) && (entry.version >= minimum_version); });
    }
}
