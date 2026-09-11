/// @file api/kmx/aio/readiness/knx/gateway.hpp
/// @brief Readiness-facing KNX gateway alias.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_READINESS) && defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/knx/gateway.hpp>
    #endif

namespace kmx::aio::readiness::knx
{
    /// @brief The KNX gateway, named in the readiness namespace.
    /// @note The same type as @ref kmx::aio::knx::gateway, not a distinct one: the KNX stack is
    ///       executor-neutral, so only its transport differs between the two I/O models.
    using gateway = kmx::aio::knx::gateway;
}
#endif // KMX_AIO_FEATURE_READINESS && KMX_AIO_FEATURE_KNX
