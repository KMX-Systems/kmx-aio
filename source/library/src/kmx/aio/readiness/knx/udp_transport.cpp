/// @file kmx/aio/readiness/knx/udp_transport.cpp
/// @brief The compiled body of the epoll KNX UDP transport.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/readiness/knx/udp_transport.hpp>

namespace kmx::aio::readiness::knx
{
    task_returning_expected_size_t udp_transport::receive_until(const span_byte_t buffer, kmx::aio::knx::transport_peer& peer,
                                                                const std::uint32_t deadline_ms) noexcept(false)
    {
        const auto result = co_await endpoint_.recv_until(buffer, peer.address, peer.length, deadline_ms);
        if (!result && (result.error().value() == ETIMEDOUT))
            co_return std::unexpected(kmx::aio::knx::make_error_code(kmx::aio::knx::error::timeout));
        co_return result;
    }

    /// @brief Sends a group's traffic out of the interface it was joined on, when one is named.
    /// @param fd The socket.
    /// @param interface_index The interface; zero leaves the choice to the kernel's multicast route.
    /// @return Nothing, or the reason the interface could not be selected.
    /// @note Without it the membership and the sends part ways: a group joined on the bus interface would still
    ///       be written to whichever interface the multicast route names.
    [[nodiscard]] static expected_void_t select_multicast_interface(const int fd, const std::uint32_t interface_index) noexcept
    {
        if (interface_index == 0u)
            return {};
#if defined(__linux__)
        ip_mreqn request {};
        request.imr_ifindex = static_cast<int>(interface_index);
        if (::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &request, sizeof(request)) < 0)
            return std::unexpected(error_from_errno());
        return {};
#else
        return std::unexpected(kmx::aio::knx::make_error_code(kmx::aio::knx::error::invalid_configuration));
#endif
    }

    /// @brief Prepares a socket to share a multicast port with the other listeners on this host.
    /// @param fd The socket.
    /// @param configuration The port, interface and loopback choice.
    /// @return Nothing, or the reason the socket could not be prepared.
    /// @details Several KNX applications on one host listen to the same group and port, so the address is
    ///          shared rather than owned. Loopback stays off unless the configuration asks for it, so that a
    ///          lone sender does not hear its own multicast back.
    [[nodiscard]] static expected_void_t prepare_multicast_socket(const int fd,
                                                                  const kmx::aio::knx::multicast_group_configuration& configuration) noexcept
    {
        const int reuse = 1;
        if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
            return std::unexpected(error_from_errno());
#if defined(SO_REUSEPORT)
        static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)));
#endif

        const int loop = configuration.loopback ? 1 : 0;
        static_cast<void>(::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)));
        if (const auto selected = select_multicast_interface(fd, configuration.interface_index); !selected.has_value())
            return selected;

        sockaddr_in local {};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(configuration.port);
        if (::bind(fd, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) < 0)
        {
            // Already bound, or bound by another listener on the same port: neither prevents the
            // membership below, which is what actually decides whether this socket sees the group.
            if ((errno != EINVAL) && (errno != EADDRINUSE))
                return std::unexpected(error_from_errno());
        }
        return {};
    }

    /// @brief Joins one IPv4 multicast group on a socket.
    /// @param fd The socket.
    /// @param group The group address.
    /// @param interface_index The interface to join on; zero lets the host choose.
    /// @return Nothing, or the reason the membership could not be added.
    /// @note Only Linux can name the interface by index; elsewhere naming one is refused rather than
    ///       silently joined on whichever interface the host picks.
    [[nodiscard]] static expected_void_t add_multicast_membership(const int fd, const in_addr group,
                                                                  const std::uint32_t interface_index) noexcept
    {
#if defined(__linux__)
        ip_mreqn request {};
        request.imr_multiaddr = group;
        request.imr_address.s_addr = htonl(INADDR_ANY);
        request.imr_ifindex = static_cast<int>(interface_index);
#else
        if (interface_index != 0u)
            return std::unexpected(kmx::aio::knx::make_error_code(kmx::aio::knx::error::invalid_configuration));

        ip_mreq request {};
        request.imr_multiaddr = group;
        request.imr_interface.s_addr = htonl(INADDR_ANY);
#endif
        if (::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &request, sizeof(request)) < 0)
            return std::unexpected(error_from_errno());
        return {};
    }

    expected_void_t udp_transport::join_multicast_group(const kmx::aio::knx::multicast_group_configuration& configuration) noexcept
    {
        if (const auto valid = kmx::aio::knx::routing::validate(configuration); !valid.has_value())
            return std::unexpected(kmx::aio::knx::make_error_code(valid.error()));

        const auto fd = endpoint_.raw().get_fd();
        if (fd < 0)
            return std::unexpected(error_from_errno(EBADF));

        if (const auto prepared = prepare_multicast_socket(fd, configuration); !prepared.has_value())
            return prepared;
        return add_multicast_membership(fd, make_multicast_address(configuration.group), configuration.interface_index);
    }

    expected_void_t udp_transport::leave_multicast_group(const kmx::aio::knx::multicast_group_configuration& configuration) noexcept
    {
        if (const auto valid = kmx::aio::knx::routing::validate(configuration); !valid.has_value())
            return std::unexpected(kmx::aio::knx::make_error_code(valid.error()));

        const auto fd = endpoint_.raw().get_fd();
        if (fd < 0)
            return std::unexpected(error_from_errno(EBADF));

#if defined(__linux__)
        ip_mreqn request {};
        request.imr_multiaddr = make_multicast_address(configuration.group);
        request.imr_address.s_addr = htonl(INADDR_ANY);
        request.imr_ifindex = static_cast<int>(configuration.interface_index);
        if (::setsockopt(fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &request, sizeof(request)) < 0)
            return std::unexpected(error_from_errno());
#else
        if (configuration.interface_index != 0u)
            return std::unexpected(kmx::aio::knx::make_error_code(kmx::aio::knx::error::invalid_configuration));
        ip_mreq request {};
        request.imr_multiaddr = make_multicast_address(configuration.group);
        request.imr_interface.s_addr = htonl(INADDR_ANY);
        if (::setsockopt(fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &request, sizeof(request)) < 0)
            return std::unexpected(error_from_errno());
#endif
        return {};
    }

    in_addr udp_transport::make_multicast_address(const ipv4::storage_t& group) noexcept
    {
        const auto value = (static_cast<std::uint32_t>(group[0u]) << 24u) | (static_cast<std::uint32_t>(group[1u]) << 16u) |
                           (static_cast<std::uint32_t>(group[2u]) << 8u) | static_cast<std::uint32_t>(group[3u]);
        in_addr address {};
        address.s_addr = htonl(value);
        return address;
    }
}
