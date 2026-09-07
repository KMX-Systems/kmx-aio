/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/knx/udp_transport.hpp>
#include <kmx/aio/completion/udp/endpoint.hpp>
#include <kmx/aio/file_descriptor.hpp>
#include <kmx/aio/knx/routing.hpp>
#if defined(KMX_AIO_FEATURE_READINESS)
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/readiness/knx/udp_transport.hpp>
    #include <kmx/aio/readiness/udp/endpoint.hpp>
#endif

#include <memory>
#include <netinet/in.h>

namespace kmx::aio::test::knx::routing_transport_test
{
    using namespace kmx::aio::knx;

    namespace
    {
        [[nodiscard]] port_t reserve_udp_port()
        {
            auto socket = file_descriptor::create_socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
            REQUIRE(socket.has_value());
            sockaddr_in local {};
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            local.sin_port = htons(0u);
            REQUIRE(socket->bind(reinterpret_cast<const sockaddr*>(&local), sizeof(local)).has_value());

            sockaddr_in bound {};
            socklen_t length = sizeof(bound);
            REQUIRE(::getsockname(socket->get(), reinterpret_cast<sockaddr*>(&bound), &length) == 0);
            return ntohs(bound.sin_port);
        }
    }

    TEST_CASE("knx completion transport can join and leave multicast group", "[knx][routing][completion][integration]")
    {
        completion::executor executor;
        auto endpoint = completion::udp::endpoint::create(executor, AF_INET);
        REQUIRE(endpoint.has_value());

        completion::knx::udp_transport transport {*endpoint};
        const kmx::aio::knx::routing::multicast_configuration configuration {
            .group = {224u, 0u, 23u, 12u},
            .port = reserve_udp_port(),
            .interface_index = 0u,
        };

        REQUIRE(transport.join_multicast_group(configuration).has_value());
        REQUIRE(transport.leave_multicast_group(configuration).has_value());
    }

#if defined(KMX_AIO_FEATURE_READINESS)
    TEST_CASE("knx readiness transport can join and leave multicast group", "[knx][routing][readiness][integration]")
    {
        auto executor = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 1u, .timeout_ms = 20u});
        auto endpoint = readiness::udp::endpoint::create(*executor, AF_INET);
        REQUIRE(endpoint.has_value());

        readiness::knx::udp_transport transport {*endpoint};
        const kmx::aio::knx::routing::multicast_configuration configuration {
            .group = {224u, 0u, 23u, 12u},
            .port = reserve_udp_port(),
            .interface_index = 0u,
        };

        REQUIRE(transport.join_multicast_group(configuration).has_value());
        REQUIRE(transport.leave_multicast_group(configuration).has_value());
        executor->stop();
    }
#endif
}
