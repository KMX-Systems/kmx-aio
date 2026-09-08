/// @file aio/readiness/knx/client.hpp
/// @brief Readiness-facing KNX tunnelling client alias.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_READINESS) && defined(KMX_AIO_FEATURE_KNX)

    #include <kmx/aio/knx/client.hpp>

namespace kmx::aio::readiness::knx
{
    using client = kmx::aio::knx::tunnelling_client;
}
#endif // KMX_AIO_FEATURE_READINESS && KMX_AIO_FEATURE_KNX
