/// @file aio/completion/knx/udp_transport.hpp
/// @brief Completion UDP adapter for the executor-neutral KNX transport contract.
#pragma once

#include <chrono>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <kmx/aio/knx/transport.hpp>
#include <kmx/aio/knx/error.hpp>
#include <kmx/aio/knx/routing.hpp>
#include <kmx/aio/completion/udp/endpoint.hpp>

namespace kmx::aio::completion::knx
{
    class udp_transport final: public kmx::aio::knx::datagram_transport
    {
    public:
        explicit udp_transport(udp::endpoint& endpoint) noexcept: endpoint_(endpoint) {}

        [[nodiscard]] task_returning_expected_size_t send(
            const cspan_byte_t payload, const sockaddr* peer, const ::socklen_t peer_length) noexcept(false) override
        {
            co_return co_await endpoint_.send(payload, peer, peer_length);
        }

        [[nodiscard]] task_returning_expected_size_t receive(
            const span_byte_t buffer, kmx::aio::knx::transport_peer& peer) noexcept(false) override
        {
            co_return co_await endpoint_.recv(buffer, peer.address, peer.length);
        }

        [[nodiscard]] task_returning_expected_size_t receive_until(
            const span_byte_t buffer, kmx::aio::knx::transport_peer& peer,
            const std::uint32_t deadline_ms) noexcept(false) override
        {
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            const auto now_ms = static_cast<std::uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
            const auto remaining_ms = static_cast<std::int32_t>(deadline_ms - now_ms);
            if (remaining_ms <= 0)
                co_return std::unexpected(kmx::aio::knx::make_error_code(kmx::aio::knx::error::timeout));
            const auto result = co_await endpoint_.recv_until(
                buffer, peer.address, peer.length,
                static_cast<std::uint64_t>(remaining_ms) * 1'000'000u);
            if (!result && (result.error().value() == ETIMEDOUT))
                co_return std::unexpected(kmx::aio::knx::make_error_code(kmx::aio::knx::error::timeout));
            co_return result;
        }

        [[nodiscard]] expected_void_t join_multicast_group(
            const kmx::aio::knx::multicast_group_configuration& configuration) noexcept override
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

        [[nodiscard]] expected_void_t leave_multicast_group(
            const kmx::aio::knx::multicast_group_configuration& configuration) noexcept override
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

    private:
        [[nodiscard]] static in_addr make_multicast_address(
            const std::array<std::uint8_t, 4u>& group) noexcept
        {
            const auto value = (static_cast<std::uint32_t>(group[0u]) << 24u) |
                               (static_cast<std::uint32_t>(group[1u]) << 16u) |
                               (static_cast<std::uint32_t>(group[2u]) << 8u) |
                               static_cast<std::uint32_t>(group[3u]);
            in_addr address {};
            address.s_addr = htonl(value);
            return address;
        }

        udp::endpoint& endpoint_;
    };
}
