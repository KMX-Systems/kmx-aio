#include <kmx/aio/basic_types.hpp>

namespace kmx::aio
{
    ip_address_owned_t to_owned_ip_address(const ip_address_t ip) noexcept
    {
        return std::visit(
            [](const auto& bytes) noexcept -> ip_address_owned_t
            {
                using ip_t = std::decay_t<decltype(bytes)>;
                if constexpr (std::is_same_v<ip_t, ipv4_address_t>)
                {
                    ipv4_address_owned_t ip4 {};
                    std::memcpy(ip4.data(), bytes.data(), ip4.size());
                    return ip4;
                }

                ipv6_address_owned_t ip6 {};
                std::memcpy(ip6.data(), bytes.data(), ip6.size());
                return ip6;
            },
            ip);
    }

    ip_address_t to_ip_address_view(const ip_address_owned_t& ip) noexcept
    {
        return std::visit(
            [](const auto& bytes) noexcept -> ip_address_t
            {
                using ip_t = std::decay_t<decltype(bytes)>;
                if constexpr (std::is_same_v<ip_t, ipv4_address_owned_t>)
                    return ipv4_address_t {bytes};
                else
                    return ipv6_address_t {bytes};
            },
            ip);
    }

    std::string ip_to_string(const ip_address_t ip) noexcept
    {
        char buffer[INET6_ADDRSTRLEN] {};

        const bool ok = std::visit(
            [&buffer](const auto& bytes) noexcept
            {
                using ip_t = std::decay_t<decltype(bytes)>;
                if constexpr (std::is_same_v<ip_t, ipv4_address_t>)
                {
                    in_addr addr {};
                    std::memcpy(&addr, bytes.data(), bytes.size());
                    return ::inet_ntop(AF_INET, &addr, buffer, sizeof(buffer)) != nullptr;
                }

                in6_addr addr {};
                std::memcpy(&addr, bytes.data(), bytes.size());
                return ::inet_ntop(AF_INET6, &addr, buffer, sizeof(buffer)) != nullptr;
            },
            ip);

        return ok ? std::string(buffer) : std::string {};
    }

    std::expected<socket_address, std::error_code> make_socket_address(const ip_address_t ip, const port_t port) noexcept
    {
        socket_address result {};

        std::visit(
            [&result, port](const auto& bytes) noexcept
            {
                using ip_t = std::decay_t<decltype(bytes)>;
                if constexpr (std::is_same_v<ip_t, ipv4_address_t>)
                {
                    auto* addr = reinterpret_cast<::sockaddr_in*>(&result.storage);
                    addr->sin_family = AF_INET;
                    addr->sin_port = ::htons(port);
                    std::memcpy(&addr->sin_addr, bytes.data(), bytes.size());
                    result.length = sizeof(::sockaddr_in);
                }
                else
                {
                    auto* addr = reinterpret_cast<sockaddr_in6*>(&result.storage);
                    addr->sin6_family = AF_INET6;
                    addr->sin6_port = ::htons(port);
                    std::memcpy(&addr->sin6_addr, bytes.data(), bytes.size());
                    result.length = sizeof(sockaddr_in6);
                }
            },
            ip);

        return result;
    }

    std::expected<socket_address, std::error_code> make_socket_address(const ip_address_owned_t& ip, const port_t port) noexcept
    {
        return make_socket_address(to_ip_address_view(ip), port);
    }

    std::expected<endpoint_address, std::error_code> parse_socket_address(const socket_address& address) noexcept
    {
        if (address.length < sizeof(sockaddr))
            return std::unexpected(error_from_errno(EINVAL));

        const auto* addr = reinterpret_cast<const sockaddr*>(&address.storage);
        endpoint_address result {};
        switch (addr->sa_family)
        {
            case AF_INET:
            {
                if (address.length < sizeof(::sockaddr_in))
                    return std::unexpected(error_from_errno(EINVAL));

                const auto* addr4 = reinterpret_cast<const ::sockaddr_in*>(&address.storage);
                auto& ip4 = result.ip.emplace<ipv4_address_owned_t>();
                std::memcpy(ip4.data(), &addr4->sin_addr, ip4.size());
                result.port = ::ntohs(addr4->sin_port);
                return result;
            }
            case AF_INET6:
            {
                if (address.length < sizeof(sockaddr_in6))
                    return std::unexpected(error_from_errno(EINVAL));

                const auto* addr6 = reinterpret_cast<const sockaddr_in6*>(&address.storage);
                auto& ip6 = result.ip.emplace<ipv6_address_owned_t>();
                std::memcpy(ip6.data(), &addr6->sin6_addr, ip6.size());
                result.port = ::ntohs(addr6->sin6_port);
                return result;
            }
            default:
                return std::unexpected(error_from_errno(EAFNOSUPPORT));
        }
    }
}