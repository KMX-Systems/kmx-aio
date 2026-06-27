/// @file completion/avb/eth_socket.hpp
/// @brief Completion-model alias for the AVB raw Ethernet socket.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <kmx/aio/avb/eth_socket.hpp>
#include <kmx/aio/completion/executor.hpp>

namespace kmx::aio::completion::avb
{
    /// @brief Completion-model raw Ethernet socket for AVB/TSN.
    using eth_socket = kmx::aio::avb::generic_eth_socket<kmx::aio::completion::executor>;
}
