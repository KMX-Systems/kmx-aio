/// @file aio/completion/knx/server.hpp
/// @brief Completion-facing KNX tunnelling server alias.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_COMPLETION) && defined(KMX_AIO_FEATURE_KNX)

    #include <kmx/aio/knx/server.hpp>

namespace kmx::aio::completion::knx
{
    /// @brief The KNX tunnelling server, named in the completion namespace.
    /// @note The same type as @ref kmx::aio::knx::generic_server, not a distinct one: the KNX stack is
    ///       executor-neutral, so only its transport differs between the two I/O models.
    using server = kmx::aio::knx::generic_server;
}
#endif // KMX_AIO_FEATURE_COMPLETION && KMX_AIO_FEATURE_KNX
