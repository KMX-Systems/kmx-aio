/// @file src/kmx/aio/knx/transport_test.cpp
/// @brief Unit test that the KNX datagram transport contract is executor-neutral.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/transport.hpp>
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/datagram_transport.hpp>
    #include <kmx/aio/task.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <sys/socket.h>
#endif

namespace kmx::aio::test::knx::transport_test
{
    using namespace kmx::aio::knx;

    namespace detail
    {
        /// @brief A transport whose operations complete at once, and do nothing.
        class stub final: public datagram_transport
        {
        public:
            [[nodiscard]] task_returning_expected_size_t send(const cspan_byte_t payload, const sockaddr*,
                                                              const ::socklen_t) noexcept(false) override
            {
                co_return expected_size_t {payload.size()};
            }

            [[nodiscard]] task_returning_expected_size_t receive(const span_byte_t, transport_peer&) noexcept(false) override
            {
                co_return expected_size_t {0u};
            }
        };
    }

    TEST_CASE("knx datagram transport is executor neutral", "[knx][transport][unit]")
    {
        detail::stub transport;
        const auto send_task = transport.send({}, nullptr, 0u);
        CHECK(send_task.valid());
        CHECK(!send_task.done());

        transport_peer peer {};
        const auto receive_task = transport.receive({}, peer);
        CHECK(receive_task.valid());
        CHECK(!receive_task.done());
    }
}
