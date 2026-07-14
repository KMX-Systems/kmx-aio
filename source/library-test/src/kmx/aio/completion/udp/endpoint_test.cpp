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

namespace kmx::aio::completion::udp
{
    struct endpoint_roundtrip_state
    {
        bool ok {};
        std::error_code error {};
        std::size_t bytes_sent {};
        std::size_t bytes_recv {};
        std::string peer_ip {};
        port_t peer_port {};
    };

    static auto run_endpoint_roundtrip(executor& exec, std::shared_ptr<endpoint_roundtrip_state> state) -> task<void>
    {
        auto recv_endpoint = endpoint::create(exec, AF_INET);
        if (!recv_endpoint)
        {
            state->error = recv_endpoint.error();
            exec.stop();
            co_return;
        }

        auto send_endpoint = endpoint::create(exec, AF_INET);
        if (!send_endpoint)
        {
            state->error = send_endpoint.error();
            exec.stop();
            co_return;
        }

        if (const auto bind_res = recv_endpoint->raw().bind(make_ipv4_address(any_ipv4), 0u); !bind_res)
        {
            state->error = bind_res.error();
            exec.stop();
            co_return;
        }

        sockaddr_in bound_addr {};
        auto bound_len = static_cast<socklen_t>(sizeof(bound_addr));
        if (::getsockname(recv_endpoint->raw().get_fd(), reinterpret_cast<sockaddr*>(&bound_addr), &bound_len) != 0)
        {
            state->error = error_from_errno();
            exec.stop();
            co_return;
        }

        const port_t recv_port = ::ntohs(bound_addr.sin_port);

        static constexpr std::array<std::byte, 8u> payload {
            std::byte {0x01}, std::byte {0x02}, std::byte {0x03}, std::byte {0x04},
            std::byte {0x05}, std::byte {0x06}, std::byte {0x07}, std::byte {0x08},
        };

        const auto send_res =
            co_await send_endpoint->send(std::span<const std::byte>(payload), make_ipv4_address(localhost_ipv4), recv_port);
        if (!send_res)
        {
            state->error = send_res.error();
            exec.stop();
            co_return;
        }
        state->bytes_sent = *send_res;

        std::array<std::byte, 32u> recv_buffer {};
        sockaddr_storage peer_addr {};
        socklen_t peer_addr_len = 0u;
        ip_address_t peer_ip = make_ipv4_address(any_ipv4);
        port_t peer_port = 0u;

        const auto recv_res = co_await recv_endpoint->recv(std::span<std::byte>(recv_buffer), peer_addr, peer_addr_len, peer_ip, peer_port);
        if (!recv_res)
        {
            state->error = recv_res.error();
            exec.stop();
            co_return;
        }
        state->bytes_recv = *recv_res;

        if (state->bytes_recv != payload.size())
        {
            state->error = std::make_error_code(std::errc::io_error);
            exec.stop();
            co_return;
        }

        if (std::memcmp(recv_buffer.data(), payload.data(), payload.size()) != 0)
        {
            state->error = std::make_error_code(std::errc::bad_message);
            exec.stop();
            co_return;
        }

        state->peer_ip = ip_to_string(peer_ip);
        state->peer_port = peer_port;
        state->ok = true;
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
} // namespace kmx::aio::completion::udp
