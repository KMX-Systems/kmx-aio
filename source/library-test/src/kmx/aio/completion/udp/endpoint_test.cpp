/// @file completion/udp/endpoint_test.cpp
/// @brief Tests for completion::udp::endpoint parity API.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <string>

#include <netinet/in.h>
#include <sys/socket.h>

#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/udp/endpoint.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::test::completion::udp::endpoint_test
{
    using namespace kmx::aio::completion;
    using namespace kmx::aio::completion::udp;

    struct endpoint_roundtrip_state
    {
        bool ok {};
        std::error_code error {};
        std::size_t bytes_sent {};
        std::size_t bytes_recv {};
        std::string peer_ip {};
        port_t peer_port {};
    };

    /// @brief The datagram this test sends and expects back unchanged.
    inline constexpr std::array<std::byte, 8u> roundtrip_payload {
        std::byte {0x01}, std::byte {0x02}, std::byte {0x03}, std::byte {0x04},
        std::byte {0x05}, std::byte {0x06}, std::byte {0x07}, std::byte {0x08},
    };

    /// @brief Binds an endpoint to an ephemeral port and reports the port the kernel chose.
    /// @param value The endpoint to bind.
    /// @return The bound port, or the reason it could not be bound.
    [[nodiscard]] static std::expected<port_t, std::error_code> bind_ephemeral(endpoint& value)
    {
        if (const auto bound = value.raw().bind(ipv4::make_address(ipv4::any), 0u); !bound)
            return std::unexpected(bound.error());

        sockaddr_in address {};
        auto length = static_cast<socklen_t>(sizeof(address));
        if (::getsockname(value.raw().get_fd(), reinterpret_cast<sockaddr*>(&address), &length) != 0)
            return std::unexpected(error_from_errno());
        return ::ntohs(address.sin_port);
    }

    /// @brief Receives the datagram and checks it arrived whole and unchanged.
    /// @param receiver The bound receiving endpoint.
    /// @param state Receives what arrived and from where.
    /// @return Nothing, or the reason it did not arrive as sent.
    static auto expect_payload(endpoint& receiver, endpoint_roundtrip_state& state) -> task<expected_void_t>
    {
        std::array<std::byte, 32u> buffer {};
        sockaddr_storage peer_address {};
        socklen_t peer_address_length {};
        ip_address_t peer_ip = ipv4::make_address(ipv4::any);
        port_t peer_port {};

        const auto received = co_await receiver.recv(span_byte_t(buffer), peer_address, peer_address_length, peer_ip, peer_port);
        if (!received)
            co_return std::unexpected(received.error());

        state.bytes_recv = *received;
        if (state.bytes_recv != roundtrip_payload.size())
            co_return std::unexpected(std::make_error_code(std::errc::io_error));
        if (std::memcmp(buffer.data(), roundtrip_payload.data(), roundtrip_payload.size()) != 0)
            co_return std::unexpected(std::make_error_code(std::errc::bad_message));

        state.peer_ip = ip_to_string(peer_ip);
        state.peer_port = peer_port;
        co_return expected_void_t {};
    }

    /// @brief Sends one datagram between two endpoints on the loopback and checks it arrives.
    static auto exchange(executor& exec, endpoint_roundtrip_state& state) -> task<expected_void_t>
    {
        auto receiver = endpoint::create(exec, AF_INET);
        if (!receiver)
            co_return std::unexpected(receiver.error());

        auto sender = endpoint::create(exec, AF_INET);
        if (!sender)
            co_return std::unexpected(sender.error());

        const auto port = bind_ephemeral(*receiver);
        if (!port.has_value())
            co_return std::unexpected(port.error());

        const auto sent = co_await sender->send(cspan_byte_t(roundtrip_payload), ipv4::make_address(ipv4::localhost), *port);
        if (!sent)
            co_return std::unexpected(sent.error());

        state.bytes_sent = *sent;
        co_return co_await expect_payload(*receiver, state);
    }

    static auto run_endpoint_roundtrip(executor& exec, std::shared_ptr<endpoint_roundtrip_state> state) -> task<void>
    {
        const auto result = co_await exchange(exec, *state);
        if (result.has_value())
            state->ok = true;
        else
            state->error = result.error();

        exec.stop();
    }

    // Note: a dedicated "null executor" validation test previously existed here. It is no
    // longer representable now that endpoint::create() takes `executor&` instead of
    // `std::shared_ptr<executor>` — a reference cannot be null by construction.

    TEST_CASE("completion udp endpoint loopback roundtrip", "[completion][udp][endpoint]")
    {
        executor exec;
        auto state = std::make_shared<endpoint_roundtrip_state>();

        exec.spawn(run_endpoint_roundtrip(exec, state));
        exec.run();

        REQUIRE(state->ok);
        REQUIRE(state->error.value() == 0);
        REQUIRE(state->bytes_sent == 8u);
        REQUIRE(state->bytes_recv == 8u);
        REQUIRE(state->peer_ip == "127.0.0.1");
        REQUIRE(state->peer_port != 0u);
    }
} // namespace kmx::aio::test::completion::udp::endpoint_test
