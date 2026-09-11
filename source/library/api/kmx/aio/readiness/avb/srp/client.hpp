/// @file api/kmx/aio/readiness/avb/srp/client.hpp
/// @brief Readiness-model alias for AVB SRP client.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_READINESS) && defined(KMX_AIO_FEATURE_AVB)
    #ifndef PCH
        #include <kmx/aio/avb/srp/generic_client.hpp>
        #include <kmx/aio/readiness/executor.hpp>
    #endif

namespace kmx::aio::readiness::avb::srp
{
    /// @brief AVB SRP client driven by the readiness-model executor.
    using client = kmx::aio::avb::srp::generic_client<kmx::aio::readiness::executor>;
}
#endif // KMX_AIO_FEATURE_READINESS && KMX_AIO_FEATURE_AVB
