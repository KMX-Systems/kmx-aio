/// @file aio/basic_types_test.cpp
/// @brief Unit tests for the IP address vocabulary and sockaddr conversions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>

#include <kmx/aio/basic_types.hpp>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>

namespace kmx::aio
{
    namespace
    {
        constexpr ipv4_storage_t ipv4_documentation {192u, 0u, 2u, 33u};
        constexpr ipv6_storage_t ipv6_documentation {0x20u, 0x01u, 0x0du, 0xb8u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0x01u};
        constexpr ipv6_storage_t ipv6_loopback {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u};
    }

    TEST_CASE("ip_family reports the family of a view", "[core][basic_types][family]")
    {
        CHECK(ip_family(make_ip_address(localhost_ipv4)) == AF_INET);
        CHECK(ip_family(make_ip_address(ipv6_loopback)) == AF_INET6);
    }

    TEST_CASE("ip_family reports the family of owned storage", "[core][basic_types][family]")
    {
        CHECK(ip_family(ip_address_owned_t {localhost_ipv4}) == AF_INET);
        CHECK(ip_family(ip_address_owned_t {ipv6_loopback}) == AF_INET6);
    }

    TEST_CASE("would_block recognises both spellings", "[core][basic_types][would_block]")
    {
        CHECK(would_block(EAGAIN));
        CHECK(would_block(EWOULDBLOCK));
        CHECK_FALSE(would_block(0));
        CHECK_FALSE(would_block(ECONNRESET));

        CHECK(would_block(error_from_errno(EAGAIN)));
        CHECK(would_block(error_from_errno(EWOULDBLOCK)));
        CHECK_FALSE(would_block(error_from_errno(EPIPE)));
    }

    TEST_CASE("error_from_errno wraps the current errno", "[core][basic_types][errno]")
    {
        errno = ECONNREFUSED;
        const auto ec = error_from_errno();
        CHECK(ec.value() == ECONNREFUSED);
        CHECK(ec.category() == std::generic_category());
        errno = 0;
    }

    TEST_CASE("error_from_errno wraps an explicit value", "[core][basic_types][errno]")
    {
        const auto ec = error_from_errno(ETIMEDOUT);
        CHECK(ec.value() == ETIMEDOUT);
        CHECK(ec.category() == std::generic_category());
    }

    TEST_CASE("to_owned_ip_address copies IPv4 bytes out of a view", "[core][basic_types][conversion]")
    {
        const auto owned = to_owned_ip_address(make_ip_address(ipv4_documentation));
        REQUIRE(std::holds_alternative<ipv4_address_owned_t>(owned));
        CHECK(std::get<ipv4_address_owned_t>(owned) == ipv4_documentation);
    }

    TEST_CASE("to_owned_ip_address copies IPv6 bytes out of a view", "[core][basic_types][conversion]")
    {
        const auto owned = to_owned_ip_address(make_ip_address(ipv6_documentation));
        REQUIRE(std::holds_alternative<ipv6_address_owned_t>(owned));
        CHECK(std::get<ipv6_address_owned_t>(owned) == ipv6_documentation);
    }

    TEST_CASE("to_owned_ip_address detaches from the source storage", "[core][basic_types][conversion]")
    {
        // The whole point of the owned form: it has to survive the buffer the view pointed at.
        ipv4_storage_t source {10u, 0u, 0u, 7u};
        const auto owned = to_owned_ip_address(make_ip_address(source));
        source = {0u, 0u, 0u, 0u};

        REQUIRE(std::holds_alternative<ipv4_address_owned_t>(owned));
        CHECK(std::get<ipv4_address_owned_t>(owned) == ipv4_storage_t {10u, 0u, 0u, 7u});
    }

    TEST_CASE("to_ip_address_view exposes owned IPv4 storage", "[core][basic_types][conversion]")
    {
        const ip_address_owned_t owned {ipv4_documentation};
        const auto view = to_ip_address_view(owned);
        REQUIRE(std::holds_alternative<ipv4_address_t>(view));

        const auto bytes = std::get<ipv4_address_t>(view);
        CHECK(bytes.data() == std::get<ipv4_address_owned_t>(owned).data());
        CHECK(std::memcmp(bytes.data(), ipv4_documentation.data(), ipv4_documentation.size()) == 0);
    }

    TEST_CASE("to_ip_address_view exposes owned IPv6 storage", "[core][basic_types][conversion]")
    {
        const ip_address_owned_t owned {ipv6_documentation};
        const auto view = to_ip_address_view(owned);
        REQUIRE(std::holds_alternative<ipv6_address_t>(view));

        const auto bytes = std::get<ipv6_address_t>(view);
        CHECK(bytes.data() == std::get<ipv6_address_owned_t>(owned).data());
        CHECK(std::memcmp(bytes.data(), ipv6_documentation.data(), ipv6_documentation.size()) == 0);
    }

    TEST_CASE("view and owned conversions round-trip", "[core][basic_types][round_trip]")
    {
        for (const ip_address_owned_t owned: {ip_address_owned_t {ipv4_documentation}, ip_address_owned_t {ipv6_documentation}})
        {
            const auto reowned = to_owned_ip_address(to_ip_address_view(owned));
            CHECK(reowned == owned);
        }
    }

    TEST_CASE("ip_to_string renders IPv4 in dotted-decimal", "[core][basic_types][to_string]")
    {
        CHECK(ip_to_string(make_ip_address(ipv4_documentation)) == "192.0.2.33");
        CHECK(ip_to_string(make_ip_address(localhost_ipv4)) == "127.0.0.1");
        CHECK(ip_to_string(make_ip_address(any_ipv4)) == "0.0.0.0");
    }

    TEST_CASE("ip_to_string renders IPv6 in compressed notation", "[core][basic_types][to_string]")
    {
        CHECK(ip_to_string(make_ip_address(ipv6_documentation)) == "2001:db8::1");
        CHECK(ip_to_string(make_ip_address(ipv6_loopback)) == "::1");
    }

    TEST_CASE("make_socket_address builds a sockaddr_in", "[core][basic_types][socket_address]")
    {
        const auto result = make_socket_address(make_ip_address(ipv4_documentation), 8080u);
        REQUIRE(result.has_value());
        CHECK(result->length == sizeof(::sockaddr_in));

        const auto* addr = reinterpret_cast<const ::sockaddr_in*>(&result->storage);
        CHECK(addr->sin_family == AF_INET);
        CHECK(::ntohs(addr->sin_port) == 8080u);
        CHECK(std::memcmp(&addr->sin_addr, ipv4_documentation.data(), ipv4_documentation.size()) == 0);
    }

    TEST_CASE("make_socket_address builds a sockaddr_in6", "[core][basic_types][socket_address]")
    {
        const auto result = make_socket_address(make_ip_address(ipv6_documentation), 443u);
        REQUIRE(result.has_value());
        CHECK(result->length == sizeof(::sockaddr_in6));

        const auto* addr = reinterpret_cast<const ::sockaddr_in6*>(&result->storage);
        CHECK(addr->sin6_family == AF_INET6);
        CHECK(::ntohs(addr->sin6_port) == 443u);
        CHECK(std::memcmp(&addr->sin6_addr, ipv6_documentation.data(), ipv6_documentation.size()) == 0);
    }

    TEST_CASE("make_socket_address accepts owned storage", "[core][basic_types][socket_address]")
    {
        const auto from_v4 = make_socket_address(ip_address_owned_t {ipv4_documentation}, 1024u);
        REQUIRE(from_v4.has_value());
        CHECK(from_v4->length == sizeof(::sockaddr_in));

        const auto from_v6 = make_socket_address(ip_address_owned_t {ipv6_documentation}, 1025u);
        REQUIRE(from_v6.has_value());
        CHECK(from_v6->length == sizeof(::sockaddr_in6));
    }

    TEST_CASE("make_socket_address keeps the port in network byte order", "[core][basic_types][socket_address]")
    {
        // Port 0 and the top of the range are where a missing htons is most visible.
        for (const port_t port: {port_t {0u}, port_t {1u}, port_t {80u}, port_t {65535u}})
        {
            CAPTURE(port);
            const auto result = make_socket_address(make_ip_address(localhost_ipv4), port);
            REQUIRE(result.has_value());
            const auto* addr = reinterpret_cast<const ::sockaddr_in*>(&result->storage);
            CHECK(::ntohs(addr->sin_port) == port);
        }
    }

    TEST_CASE("parse_socket_address recovers an IPv4 endpoint", "[core][basic_types][parse]")
    {
        const auto built = make_socket_address(make_ip_address(ipv4_documentation), 8080u);
        REQUIRE(built.has_value());

        const auto parsed = parse_socket_address(*built);
        REQUIRE(parsed.has_value());
        CHECK(parsed->port == 8080u);
        REQUIRE(std::holds_alternative<ipv4_address_owned_t>(parsed->ip));
        CHECK(std::get<ipv4_address_owned_t>(parsed->ip) == ipv4_documentation);
    }

    TEST_CASE("parse_socket_address recovers an IPv6 endpoint", "[core][basic_types][parse]")
    {
        const auto built = make_socket_address(make_ip_address(ipv6_documentation), 443u);
        REQUIRE(built.has_value());

        const auto parsed = parse_socket_address(*built);
        REQUIRE(parsed.has_value());
        CHECK(parsed->port == 443u);
        REQUIRE(std::holds_alternative<ipv6_address_owned_t>(parsed->ip));
        CHECK(std::get<ipv6_address_owned_t>(parsed->ip) == ipv6_documentation);
    }

    TEST_CASE("make_socket_address and parse_socket_address round-trip", "[core][basic_types][round_trip]")
    {
        for (const ip_address_owned_t ip: {ip_address_owned_t {localhost_ipv4}, ip_address_owned_t {ipv6_loopback},
                                           ip_address_owned_t {ipv4_documentation}, ip_address_owned_t {ipv6_documentation}})
        {
            const auto built = make_socket_address(ip, 9000u);
            REQUIRE(built.has_value());

            const auto parsed = parse_socket_address(*built);
            REQUIRE(parsed.has_value());
            CHECK(parsed->ip == ip);
            CHECK(parsed->port == 9000u);
        }
    }

    TEST_CASE("parse_socket_address rejects a length below sockaddr", "[core][basic_types][parse][error]")
    {
        socket_address address {};
        address.length = sizeof(::sockaddr) - 1u;

        const auto parsed = parse_socket_address(address);
        REQUIRE_FALSE(parsed.has_value());
        CHECK(parsed.error() == std::errc::invalid_argument);
    }

    TEST_CASE("the IPv4 arm cannot be reached with a truncated length", "[core][basic_types][parse][error]")
    {
        // On Linux sockaddr and sockaddr_in are both 16 bytes, so the leading `length < sizeof(sockaddr)`
        // guard already rejects everything the AF_INET arm's own length check would have caught - that
        // second check is unreachable here. Pinning the sizes says why, so a coverage gap on that line is
        // read as platform arithmetic rather than a missing test.
        STATIC_REQUIRE(sizeof(::sockaddr) == sizeof(::sockaddr_in));

        socket_address address {};
        address.storage.ss_family = AF_INET;
        address.length = sizeof(::sockaddr) - 1u;

        const auto parsed = parse_socket_address(address);
        REQUIRE_FALSE(parsed.has_value());
        CHECK(parsed.error() == std::errc::invalid_argument);
    }

    TEST_CASE("parse_socket_address rejects a truncated sockaddr_in6", "[core][basic_types][parse][error]")
    {
        auto built = make_socket_address(make_ip_address(ipv6_documentation), 443u);
        REQUIRE(built.has_value());
        built->length = sizeof(::sockaddr_in);

        const auto parsed = parse_socket_address(*built);
        REQUIRE_FALSE(parsed.has_value());
        CHECK(parsed.error() == std::errc::invalid_argument);
    }

    TEST_CASE("parse_socket_address rejects an unsupported family", "[core][basic_types][parse][error]")
    {
        socket_address address {};
        address.storage.ss_family = AF_UNIX;
        address.length = sizeof(::sockaddr_storage);

        const auto parsed = parse_socket_address(address);
        REQUIRE_FALSE(parsed.has_value());
        CHECK(parsed.error() == error_from_errno(EAFNOSUPPORT));
    }
}
