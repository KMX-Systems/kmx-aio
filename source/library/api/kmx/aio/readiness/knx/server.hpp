/// @file api/kmx/aio/readiness/knx/server.hpp
/// @brief Readiness-facing KNX tunnelling server alias.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_READINESS) && defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/knx/generic_server.hpp>
    #endif

namespace kmx::aio::readiness::knx
{
    /// @brief The KNX tunnelling server, named in the readiness namespace.
    /// @note The same type as @ref kmx::aio::knx::generic_server, not a distinct one: the KNX stack is
    ///       executor-neutral, so only its transport differs between the two I/O models.
    using server = kmx::aio::knx::generic_server;
}
#endif // KMX_AIO_FEATURE_READINESS && KMX_AIO_FEATURE_KNX
