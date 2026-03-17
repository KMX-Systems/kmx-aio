/// @file aio/readiness/udp/socket.hpp
/// @brief Readiness-model UDP socket using epoll-based async I/O.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/udp/socket.hpp>
    #include <kmx/aio/udp/endpoint.hpp>
#endif

namespace kmx::aio::readiness::udp
{
    /// @brief The readiness-model UDP socket is the existing epoll-based socket.
    using socket = kmx::aio::udp::socket;

    /// @brief The readiness-model UDP endpoint is the existing epoll-based endpoint.
    using endpoint = kmx::aio::udp::endpoint;

} // namespace kmx::aio::readiness::udp
