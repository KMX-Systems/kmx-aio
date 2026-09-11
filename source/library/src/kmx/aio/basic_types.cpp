/// @file src/kmx/aio/basic_types.cpp
/// @brief IP address conversions between owned and view forms, text formatting, and socket address mapping.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/basic_types.hpp>

namespace kmx::aio
{
    /// @brief Copies one IP address view alternative into its owned counterpart.
    template <typename ip_t>
    [[nodiscard]] static ip_address_owned_t own_address_bytes(const ip_t& bytes) noexcept
    {
        if constexpr (std::is_same_v<ip_t, ipv4::address_t>)
        {
            ipv4::address_owned_t ip4 {};
            std::memcpy(ip4.data(), bytes.data(), ip4.size());
            return ip4;
        }

        ipv6::address_owned_t ip6 {};
        std::memcpy(ip6.data(), bytes.data(), ip6.size());
        return ip6;
    }

    ip_address_owned_t to_owned_ip_address(const ip_address_t ip) noexcept
    {
        return std::visit([](const auto& bytes) noexcept { return own_address_bytes(bytes); }, ip);
    }

    /// @brief Makes a non-owning view over one owned IP address alternative.
    template <typename ip_t>
    [[nodiscard]] static ip_address_t view_address_bytes(const ip_t& bytes) noexcept
    {
        if constexpr (std::is_same_v<ip_t, ipv4::address_owned_t>)
            return ipv4::address_t {bytes};
        else
            return ipv6::address_t {bytes};
    }

    ip_address_t to_ip_address_view(const ip_address_owned_t& ip) noexcept
    {
        return std::visit([](const auto& bytes) noexcept { return view_address_bytes(bytes); }, ip);
    }

    /// @brief Formats one IP address view alternative into a caller-supplied text buffer.
    template <typename ip_t>
    [[nodiscard]] static bool print_address_bytes(const ip_t& bytes, std::array<char, INET6_ADDRSTRLEN>& buffer) noexcept
    {
        if constexpr (std::is_same_v<ip_t, ipv4::address_t>)
        {
            in_addr addr {};
            std::memcpy(&addr, bytes.data(), bytes.size());
            return ::inet_ntop(AF_INET, &addr, buffer.data(), buffer.size()) != nullptr;
        }

        in6_addr addr {};
        std::memcpy(&addr, bytes.data(), bytes.size());
        return ::inet_ntop(AF_INET6, &addr, buffer.data(), buffer.size()) != nullptr;
    }

    std::string ip_to_string(const ip_address_t ip) noexcept
    {
        std::array<char, INET6_ADDRSTRLEN> buffer {};

        const bool ok = std::visit([&buffer](const auto& bytes) noexcept { return print_address_bytes(bytes, buffer); }, ip);

        // LCOV_EXCL_BR_LINE: inet_ntop fails only on an unknown family or a buffer too small, and the
        // visitor above passes AF_INET or AF_INET6 with an INET6_ADDRSTRLEN buffer. The empty string
        // stays as the answer for a caller that somehow gets neither.
        return ok ? std::string(buffer.data()) : std::string {}; // LCOV_EXCL_BR_LINE
    }

    /// @brief Writes one IP address view alternative and a port into a socket address.
    template <typename ip_t>
    static void store_address_bytes(const ip_t& bytes, const port_t port, socket_address& result) noexcept
    {
        if constexpr (std::is_same_v<ip_t, ipv4::address_t>)
        {
            auto* const addr = reinterpret_cast<::sockaddr_in*>(&result.storage);
            addr->sin_family = AF_INET;
            addr->sin_port = ::htons(port);
            std::memcpy(&addr->sin_addr, bytes.data(), bytes.size());
            result.length = sizeof(::sockaddr_in);
        }
        else
        {
            auto* const addr = reinterpret_cast<sockaddr_in6*>(&result.storage);
            addr->sin6_family = AF_INET6;
            addr->sin6_port = ::htons(port);
            std::memcpy(&addr->sin6_addr, bytes.data(), bytes.size());
            result.length = sizeof(sockaddr_in6);
        }
    }

    expected_socket_address_t make_socket_address(const ip_address_t ip, const port_t port) noexcept
    {
        socket_address result {};

        std::visit([&result, port](const auto& bytes) noexcept { store_address_bytes(bytes, port, result); }, ip);

        return result;
    }

    expected_socket_address_t make_socket_address(const ip_address_owned_t& ip, const port_t port) noexcept
    {
        return make_socket_address(to_ip_address_view(ip), port);
    }

    expected_endpoint_address_t parse_socket_address(const socket_address& address) noexcept
    {
        if (address.length < sizeof(sockaddr))
            return std::unexpected(error_from_errno(EINVAL));

        const auto* const addr = reinterpret_cast<const sockaddr*>(&address.storage);
        endpoint_address result {};
        switch (addr->sa_family)
        {
            case AF_INET:
            {
                // LCOV_EXCL_START
                // Unreachable on Linux, where sockaddr and sockaddr_in are both 16 bytes: the
                // `length < sizeof(sockaddr)` test at the top of this function has already rejected
                // everything this one would catch. It is kept because that equality is a property of
                // the platform, not of the protocol. aio/basic_types_test.cpp pins it with a
                // STATIC_REQUIRE, so a platform where the two differ fails the test rather than
                // silently losing the check.
                if (address.length < sizeof(::sockaddr_in))
                    return std::unexpected(error_from_errno(EINVAL));
                // LCOV_EXCL_STOP

                const auto* addr4 = reinterpret_cast<const ::sockaddr_in*>(&address.storage);
                auto& ip4 = result.ip.emplace<ipv4::address_owned_t>();
                std::memcpy(ip4.data(), &addr4->sin_addr, ip4.size());
                result.port = ::ntohs(addr4->sin_port);
                return result;
            }
            case AF_INET6:
            {
                if (address.length < sizeof(sockaddr_in6))
                    return std::unexpected(error_from_errno(EINVAL));

                const auto* const addr6 = reinterpret_cast<const sockaddr_in6*>(&address.storage);
                auto& ip6 = result.ip.emplace<ipv6::address_owned_t>();
                std::memcpy(ip6.data(), &addr6->sin6_addr, ip6.size());
                result.port = ::ntohs(addr6->sin6_port);
                return result;
            }
            default:
                return std::unexpected(error_from_errno(EAFNOSUPPORT));
        }
    }
}