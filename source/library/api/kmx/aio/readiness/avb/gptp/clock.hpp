/// @file readiness/avb/gptp/clock.hpp
/// @brief Readiness-model alias for AVB gPTP clock.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <kmx/aio/avb/gptp/clock.hpp>
#include <kmx/aio/readiness/executor.hpp>

namespace kmx::aio::readiness::avb::gptp
{
    using clock = kmx::aio::avb::gptp::generic_clock<kmx::aio::readiness::executor>;
}
