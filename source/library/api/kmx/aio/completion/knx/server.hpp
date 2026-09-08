/// @file aio/completion/knx/server.hpp
/// @brief Completion-facing KNX tunnelling server alias.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_COMPLETION) && defined(KMX_AIO_FEATURE_KNX)

    #include <kmx/aio/knx/server.hpp>

namespace kmx::aio::completion::knx
{
    using server = kmx::aio::knx::generic_server;
}
#endif // KMX_AIO_FEATURE_COMPLETION && KMX_AIO_FEATURE_KNX
