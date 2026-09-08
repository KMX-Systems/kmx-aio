/// @file aio/readiness/knx/server.hpp
/// @brief Readiness-facing KNX tunnelling server alias.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_READINESS) && defined(KMX_AIO_FEATURE_KNX)

    #include <kmx/aio/knx/server.hpp>

namespace kmx::aio::readiness::knx
{
    using server = kmx::aio::knx::generic_server;
}
#endif // KMX_AIO_FEATURE_READINESS && KMX_AIO_FEATURE_KNX
