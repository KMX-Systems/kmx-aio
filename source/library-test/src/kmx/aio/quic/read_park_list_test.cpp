/// @file aio/quic/read_park_list_test.cpp
/// @brief Tests for detail::read_park_list, the bookkeeping behind QUIC read backpressure.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// @note The engine parks a stream when its payload pool is empty and re-arms it when a buffer comes back.
///       Driving that end to end would mean holding all 1024 pooled buffers at once over a live connection,
///       so what is tested here is the half that has behaviour rather than lsquic calls: which stream is
///       still parked, which is re-armed, and how many times. The stream pointers are never dereferenced -
///       neither by the list nor by these tests - so distinct addresses of a local array stand in for them.
#if defined(KMX_AIO_FEATURE_QUIC)

    #include <catch2/catch_test_macros.hpp>

    #include <algorithm>
    #include <array>
    #include <cstddef>
    #include <vector>

    #include <kmx/aio/quic/read_park_list.hpp>

namespace kmx::aio::test::quic::read_park_list_test::detail
{
    /// @brief Storage whose element addresses stand in for lsquic stream pointers.
    using stream_addresses_t = std::array<int, 4u>;

    /// @brief Returns the address of one slot, cast to the pointer type the list stores.
    /// @param addresses The backing storage.
    /// @param index Which slot to name.
    [[nodiscard]] inline ::lsquic_stream* stream_of(stream_addresses_t& addresses, const std::size_t index) noexcept
    {
        return reinterpret_cast<::lsquic_stream*>(&addresses[index]);
    }
} // namespace kmx::aio::test::quic::read_park_list_test::detail

namespace kmx::aio::test::quic::read_park_list_test
{
    using kmx::aio::quic::detail::read_park_list;

    TEST_CASE("quic::read_park_list starts empty", "[quic][read_park_list][unit]")
    {
        const read_park_list list;

        REQUIRE(list.empty());
        REQUIRE(list.size() == 0u);
    }

    TEST_CASE("quic::read_park_list announces only the first stream of an episode", "[quic][read_park_list][unit]")
    {
        detail::stream_addresses_t addresses {};
        read_park_list list;

        // The engine logs on this, so it has to be true once per episode of backpressure and not once per
        // stream: a busy connection with an empty pool parks every readable stream in turn.
        REQUIRE(list.park(detail::stream_of(addresses, 0u)));
        REQUIRE_FALSE(list.park(detail::stream_of(addresses, 1u)));
        REQUIRE_FALSE(list.park(detail::stream_of(addresses, 2u)));
        REQUIRE(list.size() == 3u);

        // Parking a stream that is already parked changes nothing.
        REQUIRE_FALSE(list.park(detail::stream_of(addresses, 0u)));
        REQUIRE(list.size() == 3u);
    }

    TEST_CASE("quic::read_park_list re-arms every parked stream exactly once", "[quic][read_park_list][unit]")
    {
        detail::stream_addresses_t addresses {};
        read_park_list list;

        for (std::size_t i {}; i != 3u; ++i)
            static_cast<void>(list.park(detail::stream_of(addresses, i)));

        std::vector<::lsquic_stream*> rearmed;
        const auto resumed = list.resume([&rearmed](::lsquic_stream* const stream) { rearmed.push_back(stream); });

        REQUIRE(resumed == 3u);
        REQUIRE(rearmed.size() == 3u);
        for (std::size_t i {}; i != 3u; ++i)
            REQUIRE(std::count(rearmed.begin(), rearmed.end(), detail::stream_of(addresses, i)) == 1);

        // Drained: the streams are lsquic's business again, and the next park opens a new episode.
        REQUIRE(list.empty());
        REQUIRE(list.park(detail::stream_of(addresses, 3u)));
    }

    TEST_CASE("quic::read_park_list resume on an empty list does nothing", "[quic][read_park_list][unit]")
    {
        read_park_list list;

        bool rearm_called {};
        const auto resumed = list.resume([&rearm_called](::lsquic_stream*) { rearm_called = true; });

        REQUIRE(resumed == 0u);
        REQUIRE_FALSE(rearm_called);
    }

    TEST_CASE("quic::read_park_list forgets a stream closed while parked", "[quic][read_park_list][unit]")
    {
        detail::stream_addresses_t addresses {};
        read_park_list list;

        static_cast<void>(list.park(detail::stream_of(addresses, 0u)));
        static_cast<void>(list.park(detail::stream_of(addresses, 1u)));

        // What on_close is for: lsquic has destroyed this stream, so re-arming it would touch freed memory.
        list.forget(detail::stream_of(addresses, 0u));
        REQUIRE(list.size() == 1u);

        // Forgetting a stream that was never parked is a no-op, which is what lets on_close call it blindly.
        list.forget(detail::stream_of(addresses, 2u));
        REQUIRE(list.size() == 1u);

        std::vector<::lsquic_stream*> rearmed;
        static_cast<void>(list.resume([&rearmed](::lsquic_stream* const stream) { rearmed.push_back(stream); }));

        REQUIRE(rearmed.size() == 1u);
        REQUIRE(rearmed.front() == detail::stream_of(addresses, 1u));
    }

    TEST_CASE("quic::read_park_list keeps a stream parked from inside resume", "[quic][read_park_list][unit]")
    {
        detail::stream_addresses_t addresses {};
        read_park_list list;

        static_cast<void>(list.park(detail::stream_of(addresses, 0u)));

        // Re-arming a stream makes lsquic call on_read again, which parks it right back when the pool is
        // still empty. That second park must survive this call rather than be swallowed by the drain.
        const auto resumed =
            list.resume([&list, &addresses](::lsquic_stream*) { static_cast<void>(list.park(detail::stream_of(addresses, 1u))); });

        REQUIRE(resumed == 1u);
        REQUIRE(list.size() == 1u);

        std::vector<::lsquic_stream*> rearmed;
        static_cast<void>(list.resume([&rearmed](::lsquic_stream* const stream) { rearmed.push_back(stream); }));

        REQUIRE(rearmed.size() == 1u);
        REQUIRE(rearmed.front() == detail::stream_of(addresses, 1u));
    }
} // namespace kmx::aio::test::quic::read_park_list_test

#endif // KMX_AIO_FEATURE_QUIC
