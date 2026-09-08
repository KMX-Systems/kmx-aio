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

    expected_void_t udp_transport::join_multicast_group(const kmx::aio::knx::multicast_group_configuration& configuration) noexcept
    {
        if (const auto valid = kmx::aio::knx::routing::validate(configuration); !valid.has_value())
            return std::unexpected(kmx::aio::knx::make_error_code(valid.error()));

        const auto fd = endpoint_.raw().get_fd();
        if (fd < 0)
            return std::unexpected(error_from_errno(EBADF));

        const int reuse = 1;
        if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
            return std::unexpected(error_from_errno());
#if defined(SO_REUSEPORT)
        static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)));
#endif

        sockaddr_in local {};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(configuration.port);
        if (::bind(fd, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) < 0)
        {
            if ((errno != EINVAL) && (errno != EADDRINUSE))
                return std::unexpected(error_from_errno());
        }

#if defined(__linux__)
        ip_mreqn request {};
        request.imr_multiaddr = make_multicast_address(configuration.group);
        request.imr_address.s_addr = htonl(INADDR_ANY);
        request.imr_ifindex = static_cast<int>(configuration.interface_index);
        if (::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &request, sizeof(request)) < 0)
            return std::unexpected(error_from_errno());
#else
        if (configuration.interface_index != 0u)
            return std::unexpected(kmx::aio::knx::make_error_code(kmx::aio::knx::error::invalid_configuration));
        ip_mreq request {};
        request.imr_multiaddr = make_multicast_address(configuration.group);
        request.imr_interface.s_addr = htonl(INADDR_ANY);
        if (::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &request, sizeof(request)) < 0)
            return std::unexpected(error_from_errno());
#endif
        return {};
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

    in_addr udp_transport::make_multicast_address(const std::array<std::uint8_t, 4u>& group) noexcept
    {
        const auto value = (static_cast<std::uint32_t>(group[0u]) << 24u) | (static_cast<std::uint32_t>(group[1u]) << 16u) |
                           (static_cast<std::uint32_t>(group[2u]) << 8u) | static_cast<std::uint32_t>(group[3u]);
        in_addr address {};
        address.s_addr = htonl(value);
        return address;
    }
}
