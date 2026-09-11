/// @file api/kmx/aio/readiness/knx/client.hpp
/// @brief Readiness-facing KNX tunnelling client alias.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_READINESS) && defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/knx/tunnelling_client.hpp>
    #endif

namespace kmx::aio::readiness::knx
{
    /// @brief The KNX tunnelling client, named in the readiness namespace.
    /// @note The same type as @ref kmx::aio::knx::tunnelling_client, not a distinct one: the KNX stack is
    ///       executor-neutral, so only its transport differs between the two I/O models.
    using client = kmx::aio::knx::tunnelling_client;
}
#endif // KMX_AIO_FEATURE_READINESS && KMX_AIO_FEATURE_KNX
