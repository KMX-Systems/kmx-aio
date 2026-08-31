/// @file readiness/avb/gptp/clock.hpp
/// @brief Readiness-model alias for AVB gPTP clock.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/avb/gptp/clock.hpp>
    #include <kmx/aio/readiness/executor.hpp>
#endif

namespace kmx::aio::readiness::avb::gptp
{
    /// @brief AVB gPTP clock driven by the readiness-model executor.
    using clock = kmx::aio::avb::gptp::generic_clock<kmx::aio::readiness::executor>;
}
