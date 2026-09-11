/// @file inc/kmx/aio/sample/xdp/packet_filter/manager.hpp
/// @brief Completion-model AF_XDP packet filter sample: receive-loop coroutine declaration.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/task.hpp>

    #include <atomic>
    #include <memory>
    #include <string>
#endif

namespace kmx::aio::sample::xdp::packet_filter
{
    /// @brief Receives and releases frames on one interface queue, then logs the socket statistics and stops the executor.
    /// @param exec The executor the filter runs on; stopped when the filter ends.
    /// @param ok Set once the receive loop has run.
    /// @param interface_name The network interface to attach to.
    /// @param queue_id The NIC queue to attach to.
    /// @return The filter task.
    kmx::aio::task<void> run(kmx::aio::completion::executor& exec, std::shared_ptr<std::atomic_bool> ok, std::string interface_name,
                             std::uint32_t queue_id);
}
