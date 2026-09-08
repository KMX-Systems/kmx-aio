/// @file completion/avb/eth_socket.hpp
/// @brief Completion-model alias for the AVB raw Ethernet socket.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_COMPLETION) && defined(KMX_AIO_FEATURE_AVB)
    #ifndef PCH
        #include <kmx/aio/avb/eth_socket.hpp>
        #include <kmx/aio/completion/executor.hpp>
    #endif

namespace kmx::aio::completion::avb
{
    /// @brief Completion-model raw Ethernet socket for AVB/TSN.
    using eth_socket = kmx::aio::avb::generic_eth_socket<kmx::aio::completion::executor>;
}
#endif // KMX_AIO_FEATURE_COMPLETION && KMX_AIO_FEATURE_AVB
