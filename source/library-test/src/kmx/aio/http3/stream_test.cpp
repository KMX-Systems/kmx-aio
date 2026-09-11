/// @file src/kmx/aio/http3/stream_test.cpp
/// @brief Unit tests for the HTTP/3 request stream state machine.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/http3/stream.hpp>
#ifndef PCH
    #include <kmx/aio/http3/frame.hpp>

    #include <catch2/catch_test_macros.hpp>
#endif

namespace kmx::aio::test::http3::stream_test
{
    using namespace kmx::aio::http3;

    TEST_CASE("http3 stream state machine tracks half-close transitions", "[http3][stream][state]")
    {
        stream s {0u};
        REQUIRE(s.state() == stream_state::idle);

        s.on_frame_sent(frame_type::headers);
        REQUIRE(s.state() == stream_state::open);

        s.on_send_fin();
        REQUIRE(s.state() == stream_state::half_closed_local);

        s.on_recv_fin();
        REQUIRE(s.state() == stream_state::closed);
    }
}
