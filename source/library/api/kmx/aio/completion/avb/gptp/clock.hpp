/// @file completion/avb/gptp/clock.hpp
/// @brief Completion-model alias for AVB gPTP clock.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/avb/gptp/clock.hpp>
    #include <kmx/aio/completion/executor.hpp>
#endif

namespace kmx::aio::completion::avb::gptp
{
    /// @brief AVB gPTP clock driven by the completion-model executor.
    using clock = kmx::aio::avb::gptp::generic_clock<kmx::aio::completion::executor>;
}
