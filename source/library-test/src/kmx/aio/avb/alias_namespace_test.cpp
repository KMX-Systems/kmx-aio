/// @file avb/alias_namespace_test.cpp
/// @brief Compile-time checks for AVB model-specific alias ownership.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/avb/eth_socket.hpp>
#include <kmx/aio/avb/gptp/clock.hpp>
#include <kmx/aio/avb/srp/client.hpp>
#include <kmx/aio/completion/avb/eth_socket.hpp>
#include <kmx/aio/completion/avb/gptp/clock.hpp>
#include <kmx/aio/completion/avb/srp/client.hpp>
#include <kmx/aio/readiness/avb/eth_socket.hpp>
#include <kmx/aio/readiness/avb/gptp/clock.hpp>
#include <kmx/aio/readiness/avb/srp/client.hpp>

TEST_CASE("avb model aliases map to expected generic types", "[avb][alias][compile]")
{
    static_assert(std::is_same_v<kmx::aio::completion::avb::eth_socket, kmx::aio::avb::generic_eth_socket<kmx::aio::completion::executor>>);
    static_assert(
        std::is_same_v<kmx::aio::completion::avb::gptp::clock, kmx::aio::avb::gptp::generic_clock<kmx::aio::completion::executor>>);
    static_assert(
        std::is_same_v<kmx::aio::completion::avb::srp::client, kmx::aio::avb::srp::generic_client<kmx::aio::completion::executor>>);

    static_assert(std::is_same_v<kmx::aio::readiness::avb::eth_socket, kmx::aio::avb::generic_eth_socket<kmx::aio::readiness::executor>>);
    static_assert(std::is_same_v<kmx::aio::readiness::avb::gptp::clock, kmx::aio::avb::gptp::generic_clock<kmx::aio::readiness::executor>>);
    static_assert(std::is_same_v<kmx::aio::readiness::avb::srp::client, kmx::aio::avb::srp::generic_client<kmx::aio::readiness::executor>>);

    SUCCEED();
}
