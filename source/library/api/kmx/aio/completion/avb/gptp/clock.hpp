/// @file api/kmx/aio/completion/avb/gptp/clock.hpp
/// @brief Completion-model alias for AVB gPTP clock.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_COMPLETION) && defined(KMX_AIO_FEATURE_AVB)
    #ifndef PCH
        #include <kmx/aio/avb/gptp/generic_clock.hpp>
        #include <kmx/aio/completion/executor.hpp>
    #endif

namespace kmx::aio::completion::avb::gptp
{
    /// @brief AVB gPTP clock driven by the completion-model executor.
    using clock = kmx::aio::avb::gptp::generic_clock<kmx::aio::completion::executor>;
}
#endif // KMX_AIO_FEATURE_COMPLETION && KMX_AIO_FEATURE_AVB
