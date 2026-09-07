/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/transport.hpp>

namespace kmx::aio::test::knx::transport_test
{
    using namespace kmx::aio::knx;

    class fake_transport final: public datagram_transport
    {
    public:
        [[nodiscard]] task_returning_expected_size_t send(
            const cspan_byte_t payload, const sockaddr*, const ::socklen_t) noexcept(false) override
        {
            co_return expected_size_t { payload.size() };
        }

        [[nodiscard]] task_returning_expected_size_t receive(
            const span_byte_t, transport_peer&) noexcept(false) override
        {
            co_return expected_size_t { 0u };
        }
    };

    TEST_CASE("knx datagram transport is executor neutral", "[knx][transport][unit]")
    {
        fake_transport transport;
        const auto send_task = transport.send({}, nullptr, 0u);
        CHECK(send_task.valid());
        CHECK(!send_task.done());

        transport_peer peer {};
        const auto receive_task = transport.receive({}, peer);
        CHECK(receive_task.valid());
        CHECK(!receive_task.done());
    }
}
