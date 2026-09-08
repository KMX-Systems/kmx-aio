/// @file completion/avb/srp/client.hpp
/// @brief Completion-model alias for AVB SRP client.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_COMPLETION) && defined(KMX_AIO_FEATURE_AVB)
    #ifndef PCH
        #include <kmx/aio/avb/srp/client.hpp>
        #include <kmx/aio/completion/executor.hpp>
    #endif

namespace kmx::aio::completion::avb::srp
{
    /// @brief AVB SRP client driven by the completion-model executor.
    using client = kmx::aio::avb::srp::generic_client<kmx::aio::completion::executor>;
}
#endif // KMX_AIO_FEATURE_COMPLETION && KMX_AIO_FEATURE_AVB
