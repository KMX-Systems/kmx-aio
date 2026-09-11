/// @file api/kmx/aio/readiness/tcp/connect.hpp
/// @brief Readiness-model TCP connect: a non-blocking connect that suspends until it completes.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details The completion model has `executor::async_connect`. The readiness model has no connect of its own,
///          so each client used to carry the same sequence: create a non-blocking socket, register it, start the
///          connect, wait for the socket to become writable, and read `SO_ERROR` to learn whether it worked. This
///          is that sequence, written once.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_READINESS)
    #ifndef PCH
        #include <kmx/aio/file_descriptor.hpp>
        #include <kmx/aio/readiness/executor.hpp>
        #include <kmx/aio/task.hpp>

        #include <cstdint>
        #include <sys/socket.h>
    #endif

namespace kmx::aio::readiness::tcp
{
    /// @brief Opens a TCP connection and registers the socket with @p exec.
    /// @param exec The executor the socket is registered with.
    /// @param address The peer; only @p address_length octets are read, and its family selects the socket's.
    /// @param address_length The number of octets @p address provides.
    /// @return The connected socket, registered with @p exec and with Nagle's algorithm disabled, ready to hand to
    ///         @ref kmx::aio::readiness::tcp::stream.
    /// @retval kmx::aio::error_code::operation_cancelled The wait was cancelled, by `cancel_io` or shutdown.
    /// @note Any other failure - the socket, the registration, the connect itself - is reported as the system error
    ///       behind it, and the socket is closed.
    [[nodiscard]] task<file_descriptor::expected_t> connect(executor& exec, const sockaddr* address,
                                                            ::socklen_t address_length) noexcept(false);

    /// @brief Opens a TCP connection, giving up at a deadline.
    /// @param exec The executor the socket is registered with.
    /// @param address The peer; only @p address_length octets are read.
    /// @param address_length The number of octets @p address provides.
    /// @param deadline_ms When to give up, as a monotonic millisecond stamp on the executor's clock.
    /// @return As for @ref connect.
    /// @retval std::errc::timed_out The connection was not established by the deadline; the socket is closed.
    [[nodiscard]] task<file_descriptor::expected_t> connect_until(executor& exec, const sockaddr* address, ::socklen_t address_length,
                                                                  std::uint32_t deadline_ms) noexcept(false);
}
#endif // KMX_AIO_FEATURE_READINESS
