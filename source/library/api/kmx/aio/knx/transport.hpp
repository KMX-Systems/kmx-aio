/// @file api/kmx/aio/knx/transport.hpp
/// @brief The peer address and multicast group settings the KNX datagram transport contract is written in.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/ipv4.hpp>

        #include <cstddef>
        #include <cstdint>
        #include <cstring>
        #include <sys/socket.h>
    #endif

namespace kmx::aio::knx
{
    /// @brief Non-owning peer address returned by a datagram transport.
    /// @details Storage large enough for any address family, plus the length actually written into it.
    ///          The two travel together because a `sockaddr_storage` says nothing about how much of itself
    ///          is meaningful, and code that guesses from the family reads octets the transport never set.
    struct transport_peer
    {
        /// @brief The peer address, valid in its first @ref length octets.
        sockaddr_storage address {};
        /// @brief How many octets of @ref address the transport filled in.
        ::socklen_t length = 0u;
    };

    /// @brief Copies a socket address of a caller-declared length into owned `sockaddr_storage`.
    /// @param destination The storage to fill; zeroed first, so the octets past @p length are defined.
    /// @param source The address to copy; may be smaller than `sockaddr_storage`.
    /// @param length The number of octets @p source actually provides.
    /// @return `false` when @p source is null or @p length does not fit `sockaddr_storage`.
    /// @details Only @p length octets are read. A caller holding a `sockaddr_in` has 16 octets, not the
    ///          128 of a `sockaddr_storage`, and copying the larger type out of the smaller object reads
    ///          past it - which is why the size to copy is taken from the length argument and never from
    ///          the destination type.
    [[nodiscard]] inline bool store_socket_address(sockaddr_storage& destination, const sockaddr* const source,
                                                   const ::socklen_t length) noexcept
    {
        destination = sockaddr_storage {};
        if ((source == nullptr) || (length <= 0) || (static_cast<std::size_t>(length) > sizeof(sockaddr_storage)))
            return false;

        std::memcpy(&destination, source, static_cast<std::size_t>(length));
        return true;
    }

    /// @brief The multicast group a routing endpoint joins, and the interface it joins it on and sends on.
    /// @details The defaults are the KNXnet/IP system setup: 224.0.23.12 on port 3671. A KNX installation
    ///          that segments its routing traffic overrides the group; a multi-homed host has to name the
    ///          interface as well, because the kernel's default route is rarely the one the bus is on.
    /// @reference KNX System Specifications, 03/08/02 "Core", routing multicast address.
    struct multicast_group_configuration
    {
        /// @brief The multicast group address; must be in 224.0.0.0/4.
        ipv4::storage_t group {224u, 0u, 23u, 12u};
        /// @brief The UDP port to join on.
        std::uint16_t port = 3671u;
        /// @brief The index of the interface to join the group on and to send its traffic out of; zero lets the
        ///        kernel choose both.
        std::uint32_t interface_index {};
        /// @brief Whether other listeners on this host hear what this endpoint sends to the group.
        /// @details Off by default. Turn it on when another KNX application on the same host shares the group -
        ///          ETS, xknx, a second router - or to exchange routing traffic over the loopback interface. The
        ///          routing client recognises its own traffic when the group reflects it back, either way.
        bool loopback {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
