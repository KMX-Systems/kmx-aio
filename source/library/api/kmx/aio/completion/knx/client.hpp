/// @file aio/completion/knx/client.hpp
/// @brief Completion-facing KNX tunnelling client alias.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_COMPLETION) && defined(KMX_AIO_FEATURE_KNX)

    #include <kmx/aio/knx/client.hpp>

namespace kmx::aio::completion::knx
{
    using client = kmx::aio::knx::tunnelling_client;
}
#endif // KMX_AIO_FEATURE_COMPLETION && KMX_AIO_FEATURE_KNX
