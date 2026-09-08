/// @file readiness/avb/gptp/clock.hpp
/// @brief Readiness-model alias for AVB gPTP clock.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_READINESS) && defined(KMX_AIO_FEATURE_AVB)
    #ifndef PCH
        #include <kmx/aio/avb/gptp/clock.hpp>
        #include <kmx/aio/readiness/executor.hpp>
    #endif

namespace kmx::aio::readiness::avb::gptp
{
    /// @brief AVB gPTP clock driven by the readiness-model executor.
    using clock = kmx::aio::avb::gptp::generic_clock<kmx::aio::readiness::executor>;
}
#endif // KMX_AIO_FEATURE_READINESS && KMX_AIO_FEATURE_AVB
