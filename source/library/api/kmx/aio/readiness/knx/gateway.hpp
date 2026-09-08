/// @file aio/readiness/knx/gateway.hpp
/// @brief Readiness-facing KNX gateway alias.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_READINESS) && defined(KMX_AIO_FEATURE_KNX)

    #include <kmx/aio/knx/gateway.hpp>

namespace kmx::aio::readiness::knx
{
    using gateway = kmx::aio::knx::gateway;
}
#endif // KMX_AIO_FEATURE_READINESS && KMX_AIO_FEATURE_KNX
