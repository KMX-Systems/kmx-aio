/// @file readiness/avb/srp/client.hpp
/// @brief Readiness-model alias for AVB SRP client.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/avb/srp/client.hpp>
    #include <kmx/aio/readiness/executor.hpp>
#endif

namespace kmx::aio::readiness::avb::srp
{
    /// @brief AVB SRP client driven by the readiness-model executor.
    using client = kmx::aio::avb::srp::generic_client<kmx::aio::readiness::executor>;
}
