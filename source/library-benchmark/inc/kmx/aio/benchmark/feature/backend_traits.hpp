/// @file inc/kmx/aio/benchmark/feature/backend_traits.hpp
/// @brief What the two execution models' backends share: the loopback address, and whether both are in this build.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details The executors deliberately share no base class - there is no virtual I/O anywhere in the
///          library, which is the point of it. A benchmark that wants to measure the same work on
///          both therefore has to bridge them somewhere, and doing it here, in a header only the
///          benchmark sees, keeps that bridge out of the library.
///
///          What the two models genuinely require differs, and this file preserves those differences
///          rather than papering over them. The readiness model needs its descriptors non-blocking
///          and registered with the executor before anything can wait on them; the completion model
///          needs neither. Forcing one model into the other's configuration would measure a set-up
///          nobody would ship, so each side is configured the way its own model asks to be, and only
///          the *work* is held identical.
#pragma once
#include <kmx/aio/config.hpp>
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/ipv4.hpp>

    #include <netinet/in.h>
    #include <sys/socket.h>
#endif

namespace kmx::aio::benchmark::feature
{
    /// @brief The address every scenario binds and connects to.
    /// @details Loopback throughout. These benchmarks compare two executors doing the same work, and
    ///          the wire is not part of that comparison - it would add a term neither executor
    ///          controls and that varies more between runs than the thing being measured. The
    ///          absolute figures are correspondingly optimistic against a real link; the ratio is
    ///          what the report is for.
    [[nodiscard]] inline ip_address_t loopback() noexcept
    {
        return make_ip_address(ipv4::localhost);
    }

    /// @brief Builds a loopback socket address for a port.
    /// @param port The port.
    /// @return The address, ready to hand to connect(2).
    [[nodiscard]] inline ::sockaddr_in loopback_address(const port_t port) noexcept
    {
        ::sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(port);
        addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        return addr;
    }

    /// @brief Binds a socket to a loopback port.
    /// @details Through ::bind on the descriptor rather than through the socket wrapper, because only
    ///          one of the two models has a bind() on it: completion::udp::socket does,
    ///          readiness::udp::socket does not. Reaching for the descriptor is the only thing both
    ///          models can be asked to do here, and a benchmark is the wrong place to work around a
    ///          gap in the API - it would end up measuring the workaround.
    /// @param fd The socket to bind.
    /// @param port The loopback port, or zero to let the kernel choose.
    /// @return True when the bind succeeded.
    [[nodiscard]] inline bool bind_loopback(const fd_t fd, const port_t port) noexcept
    {
        const auto addr = loopback_address(port);
        return ::bind(fd, reinterpret_cast<const ::sockaddr*>(&addr), sizeof(addr)) == 0;
    }

    /// @brief Reads back the port the kernel actually assigned to a socket bound to port zero.
    /// @details Scenarios bind to port zero rather than picking a number, so two benchmark runs - or a
    ///          benchmark and whatever else is on the machine - cannot collide on a fixed port and
    ///          turn a measurement into a bind failure.
    /// @param fd The bound socket.
    /// @return The port, or zero when it could not be read.
    [[nodiscard]] inline port_t bound_port(const fd_t fd) noexcept
    {
        ::sockaddr_in addr {};
        ::socklen_t length = sizeof(addr);
        if (::getsockname(fd, reinterpret_cast<::sockaddr*>(&addr), &length) != 0)
            return 0u;

        return ::ntohs(addr.sin_port);
    }

    /// @brief True when both models are in this build, so a pairing has two sides to measure.
    static constexpr bool both_models_present =
#if defined(KMX_AIO_FEATURE_READINESS) && defined(KMX_AIO_FEATURE_COMPLETION)
        true;
#else
        false;
#endif

}
