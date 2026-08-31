/// @file completion/avb/srp/client.hpp
/// @brief Completion-model alias for AVB SRP client.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/avb/srp/client.hpp>
    #include <kmx/aio/completion/executor.hpp>
#endif

namespace kmx::aio::completion::avb::srp
{
    /// @brief AVB SRP client driven by the completion-model executor.
    using client = kmx::aio::avb::srp::generic_client<kmx::aio::completion::executor>;
}
