/// @file readiness/avb/srp/client.hpp
/// @brief Readiness-model alias for AVB SRP client.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <kmx/aio/avb/srp/client.hpp>
#include <kmx/aio/readiness/executor.hpp>

namespace kmx::aio::readiness::avb::srp
{
    using client = kmx::aio::avb::srp::generic_client<kmx::aio::readiness::executor>;
}
