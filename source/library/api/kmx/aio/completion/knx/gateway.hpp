/// @file aio/completion/knx/gateway.hpp
/// @brief Completion-facing KNX gateway alias.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_COMPLETION) && defined(KMX_AIO_FEATURE_KNX)

    #include <kmx/aio/knx/gateway.hpp>

namespace kmx::aio::completion::knx
{
    using gateway = kmx::aio::knx::gateway;
}
#endif // KMX_AIO_FEATURE_COMPLETION && KMX_AIO_FEATURE_KNX
