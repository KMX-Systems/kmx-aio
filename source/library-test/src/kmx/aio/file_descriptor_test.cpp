/// @file aio/file_descriptor_test.cpp
/// @brief Unit tests for the file_descriptor RAII wrapper and its syscall shims.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/file_descriptor.hpp>

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace kmx::aio::test::file_descriptor_test
{
    namespace detail
    {
        // A bound, listening loopback socket on a kernel-chosen port. Port 0 keeps the tests from
        // colliding with each other or with whatever else is running on the machine.
        struct listening_socket
        {
            file_descriptor fd {};
            port_t port {};
        };

        listening_socket make_listener()
        {
            auto created = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
            REQUIRE(created.has_value());

            listening_socket result {std::move(*created), 0u};

            const int reuse = 1;
            REQUIRE(result.fd.setsockopt(SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)).has_value());
            REQUIRE(result.fd.bind(make_ip_address(ipv4::localhost), 0u).has_value());
            REQUIRE(result.fd.listen(4).has_value());

            ::sockaddr_in bound {};
            ::socklen_t length = sizeof(bound);
            REQUIRE(::getsockname(result.fd.get(), reinterpret_cast<::sockaddr*>(&bound), &length) == 0);
            result.port = ::ntohs(bound.sin_port);

            return result;
        }
    } // namespace detail

    TEST_CASE("a default-constructed descriptor owns nothing", "[core][file_descriptor][lifetime]")
    {
        const file_descriptor fd {};
        CHECK_FALSE(fd.is_valid());
        CHECK(fd.get() == file_descriptor::invalid_fd);
    }

    TEST_CASE("a descriptor adopts and closes an fd", "[core][file_descriptor][lifetime]")
    {
        std::array<int, 2> pipe_fds {};
        REQUIRE(::pipe(pipe_fds.data()) == 0);
        const int read_end = pipe_fds[0];

        {
            file_descriptor fd {read_end};
            CHECK(fd.is_valid());
            CHECK(fd.get() == read_end);
        }

        // Once the wrapper is gone the fd must be closed; fcntl on a closed fd is the cheap probe.
        CHECK(::fcntl(read_end, F_GETFD) == -1);
        CHECK(errno == EBADF);
        ::close(pipe_fds[1]);
    }

    TEST_CASE("close is idempotent", "[core][file_descriptor][lifetime]")
    {
        std::array<int, 2> pipe_fds {};
        REQUIRE(::pipe(pipe_fds.data()) == 0);

        file_descriptor fd {pipe_fds[0]};
        fd.close();
        CHECK_FALSE(fd.is_valid());

        // A second close must not touch the fd number again: by now it may have been handed to someone
        // else, and closing it a second time would take down an unrelated descriptor.
        fd.close();
        CHECK_FALSE(fd.is_valid());
        CHECK(fd.get() == file_descriptor::invalid_fd);
        ::close(pipe_fds[1]);
    }

    TEST_CASE("release hands the fd back without closing it", "[core][file_descriptor][lifetime]")
    {
        std::array<int, 2> pipe_fds {};
        REQUIRE(::pipe(pipe_fds.data()) == 0);

        int released = file_descriptor::invalid_fd;
        {
            file_descriptor fd {pipe_fds[0]};
            released = fd.release();
            CHECK_FALSE(fd.is_valid());
        }

        CHECK(released == pipe_fds[0]);
        CHECK(::fcntl(released, F_GETFD) != -1);
        ::close(released);
        ::close(pipe_fds[1]);
    }

    TEST_CASE("move construction transfers ownership", "[core][file_descriptor][move]")
    {
        std::array<int, 2> pipe_fds {};
        REQUIRE(::pipe(pipe_fds.data()) == 0);

        file_descriptor source {pipe_fds[0]};
        file_descriptor target {std::move(source)};

        CHECK(target.get() == pipe_fds[0]);
        CHECK_FALSE(source.is_valid());
        ::close(pipe_fds[1]);
    }

    TEST_CASE("move assignment closes the descriptor it replaces", "[core][file_descriptor][move]")
    {
        std::array<int, 2> first {};
        std::array<int, 2> second {};
        REQUIRE(::pipe(first.data()) == 0);
        REQUIRE(::pipe(second.data()) == 0);

        file_descriptor target {first[0]};
        file_descriptor source {second[0]};
        target = std::move(source);

        CHECK(target.get() == second[0]);
        CHECK_FALSE(source.is_valid());

        // first[0] was the descriptor target used to hold; assignment must have closed it.
        CHECK(::fcntl(first[0], F_GETFD) == -1);
        CHECK(errno == EBADF);

        ::close(first[1]);
        ::close(second[1]);
    }

    TEST_CASE("self move assignment leaves the descriptor intact", "[core][file_descriptor][move]")
    {
        std::array<int, 2> pipe_fds {};
        REQUIRE(::pipe(pipe_fds.data()) == 0);

        file_descriptor fd {pipe_fds[0]};
        auto& alias = fd;
        fd = std::move(alias);

        CHECK(fd.is_valid());
        CHECK(fd.get() == pipe_fds[0]);
        CHECK(::fcntl(fd.get(), F_GETFD) != -1);
        ::close(pipe_fds[1]);
    }

    TEST_CASE("create_socket produces a usable socket", "[core][file_descriptor][socket]")
    {
        const auto tcp = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(tcp.has_value());
        CHECK(tcp->is_valid());

        const auto udp = file_descriptor::create_socket(AF_INET6, SOCK_DGRAM, 0);
        REQUIRE(udp.has_value());
        CHECK(udp->is_valid());
    }

    TEST_CASE("create_socket reports a rejected domain", "[core][file_descriptor][socket][error]")
    {
        const auto result = file_descriptor::create_socket(-1, SOCK_STREAM, 0);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().category() == std::generic_category());
    }

    TEST_CASE("read and write move bytes through a pipe", "[core][file_descriptor][io]")
    {
        std::array<int, 2> pipe_fds {};
        REQUIRE(::pipe(pipe_fds.data()) == 0);

        file_descriptor reader {pipe_fds[0]};
        file_descriptor writer {pipe_fds[1]};

        const std::string payload {"kmx-aio"};
        const auto written = writer.write(payload.data(), payload.size());
        REQUIRE(written.has_value());
        CHECK(*written == payload.size());

        std::array<char, 32> buffer {};
        const auto read = reader.read(buffer.data(), buffer.size());
        REQUIRE(read.has_value());
        CHECK(*read == payload.size());
        CHECK(std::string(buffer.data(), *read) == payload);
    }

    TEST_CASE("read reports end of stream as zero", "[core][file_descriptor][io]")
    {
        std::array<int, 2> pipe_fds {};
        REQUIRE(::pipe(pipe_fds.data()) == 0);

        file_descriptor reader {pipe_fds[0]};
        ::close(pipe_fds[1]);

        std::array<char, 8> buffer {};
        const auto read = reader.read(buffer.data(), buffer.size());
        REQUIRE(read.has_value());
        CHECK(*read == 0u);
    }

    TEST_CASE("read fails on a write-only descriptor", "[core][file_descriptor][io][error]")
    {
        std::array<int, 2> pipe_fds {};
        REQUIRE(::pipe(pipe_fds.data()) == 0);

        file_descriptor writer {pipe_fds[1]};
        std::array<char, 8> buffer {};

        const auto read = writer.read(buffer.data(), buffer.size());
        REQUIRE_FALSE(read.has_value());
        CHECK(read.error() == std::errc::bad_file_descriptor);
        ::close(pipe_fds[0]);
    }

    TEST_CASE("write fails on a read-only descriptor", "[core][file_descriptor][io][error]")
    {
        std::array<int, 2> pipe_fds {};
        REQUIRE(::pipe(pipe_fds.data()) == 0);

        file_descriptor reader {pipe_fds[0]};
        const auto written = reader.write("x", 1u);
        REQUIRE_FALSE(written.has_value());
        CHECK(written.error() == std::errc::bad_file_descriptor);
        ::close(pipe_fds[1]);
    }

    TEST_CASE("every operation refuses an empty wrapper with EBADF", "[core][file_descriptor][error]")
    {
        // The is_valid() guard at the top of each shim: an empty wrapper must fail before it can pass
        // -1 to a syscall, where the errno would depend on the call rather than on the wrapper.
        file_descriptor fd {};
        std::array<char, 8> buffer {};
        ::sockaddr_in addr {};
        ::socklen_t length = sizeof(addr);
        int optval = 0;

        const auto bad = std::errc::bad_file_descriptor;
        CHECK(fd.fcntl(F_GETFL, 0).error() == bad);
        CHECK(fd.read(buffer.data(), buffer.size()).error() == bad);
        CHECK(fd.write(buffer.data(), buffer.size()).error() == bad);
        CHECK(fd.bind(reinterpret_cast<const ::sockaddr*>(&addr), length).error() == bad);
        CHECK(fd.setsockopt(SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)).error() == bad);
        CHECK(fd.listen(1).error() == bad);
        CHECK(fd.accept(reinterpret_cast<::sockaddr*>(&addr), &length).error() == bad);
        CHECK(fd.connect(reinterpret_cast<const ::sockaddr*>(&addr), length).error() == bad);
        CHECK(fd.getsockopt(SOL_SOCKET, SO_ERROR, &optval, &length).error() == bad);
        CHECK(fd.set_as_non_blocking().error() == bad);

        ip_address_owned_t peer_ip {};
        port_t peer_port {};
        CHECK(fd.accept(peer_ip, peer_port).error() == bad);
        CHECK(fd.bind(make_ip_address(ipv4::localhost), 0u).error() == bad);
        CHECK(fd.connect(make_ip_address(ipv4::localhost), 0u).error() == bad);
    }

    TEST_CASE("fcntl reads and writes descriptor flags", "[core][file_descriptor][fcntl]")
    {
        auto created = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(created.has_value());

        const auto flags = created->fcntl(F_GETFL, 0);
        REQUIRE(flags.has_value());
        CHECK((*flags & O_NONBLOCK) == 0);

        REQUIRE(created->fcntl(F_SETFL, *flags | O_NONBLOCK).has_value());

        const auto updated = created->fcntl(F_GETFL, 0);
        REQUIRE(updated.has_value());
        CHECK((*updated & O_NONBLOCK) != 0);
    }

    TEST_CASE("fcntl reports an unknown command", "[core][file_descriptor][fcntl][error]")
    {
        auto created = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(created.has_value());

        const auto result = created->fcntl(0x7fff, 0);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::invalid_argument);
    }

    TEST_CASE("set_as_non_blocking sets O_NONBLOCK", "[core][file_descriptor][fcntl]")
    {
        auto created = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(created.has_value());
        REQUIRE(created->set_as_non_blocking().has_value());

        const auto flags = created->fcntl(F_GETFL, 0);
        REQUIRE(flags.has_value());
        CHECK((*flags & O_NONBLOCK) != 0);
    }

    TEST_CASE("setsockopt and getsockopt agree", "[core][file_descriptor][sockopt]")
    {
        auto created = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(created.has_value());

        const int reuse = 1;
        REQUIRE(created->setsockopt(SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)).has_value());

        int read_back = 0;
        ::socklen_t length = sizeof(read_back);
        REQUIRE(created->getsockopt(SOL_SOCKET, SO_REUSEADDR, &read_back, &length).has_value());
        CHECK(read_back != 0);
    }

    TEST_CASE("setsockopt reports an unknown option", "[core][file_descriptor][sockopt][error]")
    {
        auto created = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(created.has_value());

        const int value = 1;
        const auto result = created->setsockopt(SOL_SOCKET, 0x7fff, &value, sizeof(value));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().category() == std::generic_category());
    }

    TEST_CASE("getsockopt reports an unknown option", "[core][file_descriptor][sockopt][error]")
    {
        auto created = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(created.has_value());

        int value = 0;
        ::socklen_t length = sizeof(value);
        const auto result = created->getsockopt(SOL_SOCKET, 0x7fff, &value, &length);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().category() == std::generic_category());
    }

    TEST_CASE("bind accepts the IP and port overload", "[core][file_descriptor][bind]")
    {
        auto created = file_descriptor::create_socket(AF_INET, SOCK_DGRAM, 0);
        REQUIRE(created.has_value());
        CHECK(created->bind(make_ip_address(ipv4::localhost), 0u).has_value());
    }

    TEST_CASE("bind rejects an address the host does not own", "[core][file_descriptor][bind][error]")
    {
        auto created = file_descriptor::create_socket(AF_INET, SOCK_DGRAM, 0);
        REQUIRE(created.has_value());

        // 192.0.2.0/24 is the reserved documentation range: never a local address.
        constexpr ipv4::storage_t unroutable {192u, 0u, 2u, 33u};
        const auto result = created->bind(make_ip_address(unroutable), 0u);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::address_not_available);
    }

    TEST_CASE("bind rejects a mismatched address family", "[core][file_descriptor][bind][error]")
    {
        auto created = file_descriptor::create_socket(AF_INET, SOCK_DGRAM, 0);
        REQUIRE(created.has_value());

        constexpr ipv6::storage_t loopback6 {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u};
        const auto result = created->bind(make_ip_address(loopback6), 0u);
        REQUIRE_FALSE(result.has_value());
    }

    TEST_CASE("listen rejects a datagram socket", "[core][file_descriptor][listen][error]")
    {
        auto created = file_descriptor::create_socket(AF_INET, SOCK_DGRAM, 0);
        REQUIRE(created.has_value());
        REQUIRE(created->bind(make_ip_address(ipv4::localhost), 0u).has_value());

        const auto result = created->listen(4);
        REQUIRE_FALSE(result.has_value());
    }

    TEST_CASE("connect and accept complete a loopback handshake", "[core][file_descriptor][accept]")
    {
        auto listener = detail::make_listener();

        auto client = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(client.has_value());
        REQUIRE(client->connect(make_ip_address(ipv4::localhost), listener.port).has_value());

        ::sockaddr_storage peer {};
        ::socklen_t length = sizeof(peer);
        auto accepted = listener.fd.accept(reinterpret_cast<::sockaddr*>(&peer), &length);
        REQUIRE(accepted.has_value());
        CHECK(accepted->is_valid());
        CHECK(peer.ss_family == AF_INET);
    }

    TEST_CASE("accept reports the peer through the address overload", "[core][file_descriptor][accept]")
    {
        auto listener = detail::make_listener();

        auto client = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(client.has_value());
        REQUIRE(client->connect(make_ip_address(ipv4::localhost), listener.port).has_value());

        ip_address_owned_t peer_ip {};
        port_t peer_port {};
        auto accepted = listener.fd.accept(peer_ip, peer_port);
        REQUIRE(accepted.has_value());
        CHECK(accepted->is_valid());

        REQUIRE(std::holds_alternative<ipv4::address_owned_t>(peer_ip));
        CHECK(std::get<ipv4::address_owned_t>(peer_ip) == ipv4::localhost);
        CHECK(peer_port != 0u);
    }

    TEST_CASE("accept reports an IPv6 peer through the address overload", "[core][file_descriptor][accept]")
    {
        // Drives the AF_INET6 arm of the family switch, which the IPv4 handshake never reaches.
        auto listener_fd = file_descriptor::create_socket(AF_INET6, SOCK_STREAM, 0);
        REQUIRE(listener_fd.has_value());

        const int reuse = 1;
        REQUIRE(listener_fd->setsockopt(SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)).has_value());

        constexpr ipv6::storage_t loopback6 {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u};
        REQUIRE(listener_fd->bind(make_ip_address(loopback6), 0u).has_value());
        REQUIRE(listener_fd->listen(4).has_value());

        ::sockaddr_in6 bound {};
        ::socklen_t length = sizeof(bound);
        REQUIRE(::getsockname(listener_fd->get(), reinterpret_cast<::sockaddr*>(&bound), &length) == 0);

        auto client = file_descriptor::create_socket(AF_INET6, SOCK_STREAM, 0);
        REQUIRE(client.has_value());
        REQUIRE(client->connect(make_ip_address(loopback6), ::ntohs(bound.sin6_port)).has_value());

        ip_address_owned_t peer_ip {};
        port_t peer_port {};
        auto accepted = listener_fd->accept(peer_ip, peer_port);
        REQUIRE(accepted.has_value());

        REQUIRE(std::holds_alternative<ipv6::address_owned_t>(peer_ip));
        CHECK(std::get<ipv6::address_owned_t>(peer_ip) == loopback6);
        CHECK(peer_port != 0u);
    }

    TEST_CASE("accept on an idle non-blocking listener would block", "[core][file_descriptor][accept][error]")
    {
        auto listener = detail::make_listener();
        REQUIRE(listener.fd.set_as_non_blocking().has_value());

        ip_address_owned_t peer_ip {};
        port_t peer_port {};
        const auto accepted = listener.fd.accept(peer_ip, peer_port);
        REQUIRE_FALSE(accepted.has_value());
        CHECK(would_block(accepted.error()));
    }

    TEST_CASE("connect on a non-blocking socket reports success while in progress", "[core][file_descriptor][connect]")
    {
        // EINPROGRESS is the expected answer for a non-blocking connect and the wrapper deliberately
        // swallows it, so the caller can go straight to waiting for writability.
        auto listener = detail::make_listener();

        auto client = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(client.has_value());
        REQUIRE(client->set_as_non_blocking().has_value());

        CHECK(client->connect(make_ip_address(ipv4::localhost), listener.port).has_value());
    }

    TEST_CASE("connect reports a refused port", "[core][file_descriptor][connect][error]")
    {
        // Bind a listener, learn its port, then drop it: the port is now almost certainly unused, and a
        // blocking connect to loopback fails immediately rather than hanging.
        port_t closed_port {};
        {
            auto listener = detail::make_listener();
            closed_port = listener.port;
        }

        auto client = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(client.has_value());

        const auto result = client->connect(make_ip_address(ipv4::localhost), closed_port);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::connection_refused);
    }

    TEST_CASE("inet_pton parses IPv4 text", "[core][file_descriptor][inet_pton]")
    {
        ::in_addr addr {};
        REQUIRE(aio::inet_pton(AF_INET, "192.0.2.33", &addr).has_value());

        constexpr ipv4::storage_t expected {192u, 0u, 2u, 33u};
        CHECK(std::memcmp(&addr, expected.data(), expected.size()) == 0);
    }

    TEST_CASE("inet_pton parses IPv6 text", "[core][file_descriptor][inet_pton]")
    {
        ::in6_addr addr {};
        REQUIRE(aio::inet_pton(AF_INET6, "2001:db8::1", &addr).has_value());

        constexpr ipv6::storage_t expected {0x20u, 0x01u, 0x0du, 0xb8u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0x01u};
        CHECK(std::memcmp(&addr, expected.data(), expected.size()) == 0);
    }

    TEST_CASE("inet_pton rejects malformed text as EINVAL", "[core][file_descriptor][inet_pton][error]")
    {
        // ::inet_pton returns 0 rather than -1 for unparseable text, and the shim turns that into EINVAL.
        ::in_addr addr {};
        for (const char* const text: {"", "not-an-address", "192.0.2", "192.0.2.256", "192.0.2.33.44"})
        {
            CAPTURE(text);
            const auto result = aio::inet_pton(AF_INET, text, &addr);
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error() == std::errc::invalid_argument);
        }
    }

    TEST_CASE("inet_pton rejects an unsupported family", "[core][file_descriptor][inet_pton][error]")
    {
        // A bad family is the -1 branch: ::inet_pton sets EAFNOSUPPORT and the shim forwards errno.
        ::in_addr addr {};
        const auto result = aio::inet_pton(AF_UNIX, "192.0.2.33", &addr);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == error_from_errno(EAFNOSUPPORT));
    }
} // namespace kmx::aio::test::file_descriptor_test
