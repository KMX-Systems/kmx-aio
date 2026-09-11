/// @file src/kmx/aio/knx/detail/frame_reassembler_test.cpp
/// @brief Unit tests for the KNXnet/IP over TCP frame reassembler: split reads, batched frames and untrusted headers.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/detail/frame_reassembler.hpp>
#ifndef PCH
    #include <kmx/aio/knx/error.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <algorithm>
    #include <cstdint>
    #include <optional>
    #include <span>
    #include <vector>
#endif

namespace kmx::aio::test::knx::detail::frame_reassembler_test
{
    namespace kn = kmx::aio::knx;
    using kn::error;
    using kn::make_error_code;
    namespace kd = kmx::aio::knx::detail;

    namespace detail
    {
        using octets_t = std::vector<std::uint8_t>;

        /// @brief A CONNECTIONSTATE_REQUEST on channel @p channel: an eight-octet frame.
        [[nodiscard]] octets_t heartbeat(const std::uint8_t channel) noexcept(false)
        {
            return {0x06u, 0x10u, 0x02u, 0x07u, 0x00u, 0x08u, channel, 0x00u};
        }

        /// @brief A TUNNELLING_REQUEST carrying a switch-on telegram: a 21-octet frame.
        [[nodiscard]] octets_t tunnelling_request() noexcept(false)
        {
            return {0x06u, 0x10u, 0x04u, 0x20u, 0x00u, 0x15u, 0x04u, 0x01u, 0x00u, 0x00u, 0x11u,
                    0x00u, 0xBCu, 0xE0u, 0x11u, 0x01u, 0x0Au, 0x03u, 0x01u, 0x00u, 0x81u};
        }

        /// @brief Feeds octets as one read would.
        void feed(kd::frame_reassembler& value, const cspan_uint8_t octets) noexcept(false)
        {
            const auto space = value.writable();
            REQUIRE(space.size() >= octets.size());
            std::ranges::copy(octets, space.begin());
            value.commit(octets.size());
        }

        /// @brief Takes the next frame, which must be whole, as owned octets.
        [[nodiscard]] octets_t take(kd::frame_reassembler& value) noexcept(false)
        {
            const auto frame = value.next();
            REQUIRE(frame.has_value());
            REQUIRE(frame->has_value());
            return {(*frame)->begin(), (*frame)->end()};
        }

        /// @brief Indicates whether no whole frame is available yet, without error.
        [[nodiscard]] bool waiting(kd::frame_reassembler& value) noexcept(false)
        {
            const auto frame = value.next();
            return frame.has_value() && !frame->has_value();
        }
    }

    TEST_CASE("knx frame reassembler completes a header split across reads", "[knx][tcp][unit]")
    {
        kd::frame_reassembler value {};
        const auto frame = detail::heartbeat(7u);
        detail::feed(value, std::span {frame}.first(3u));
        CHECK(detail::waiting(value));
        CHECK(value.partial());
        detail::feed(value, std::span {frame}.subspan(3u, 3u));
        CHECK(detail::waiting(value));
        detail::feed(value, std::span {frame}.subspan(6u));
        CHECK(detail::take(value) == frame);
        CHECK(!value.partial());
        CHECK(detail::waiting(value));
    }

    TEST_CASE("knx frame reassembler completes a body split across reads", "[knx][tcp][unit]")
    {
        kd::frame_reassembler value {};
        const auto frame = detail::tunnelling_request();
        detail::feed(value, std::span {frame}.first(10u));
        CHECK(detail::waiting(value));
        detail::feed(value, std::span {frame}.subspan(10u));
        CHECK(detail::take(value) == frame);
    }

    TEST_CASE("knx frame reassembler hands out every frame one read delivered", "[knx][tcp][unit]")
    {
        kd::frame_reassembler value {};
        auto stream = detail::heartbeat(1u);
        const auto second = detail::tunnelling_request();
        const auto third = detail::heartbeat(2u);
        stream.insert(stream.end(), second.begin(), second.end());
        stream.insert(stream.end(), third.begin(), third.begin() + 4); // and the start of a third

        detail::feed(value, stream);
        CHECK(detail::take(value) == detail::heartbeat(1u));
        CHECK(detail::take(value) == second);
        CHECK(detail::waiting(value));
        CHECK(value.partial());
        detail::feed(value, std::span {third}.subspan(4u));
        CHECK(detail::take(value) == third);
    }

    TEST_CASE("knx frame reassembler keeps a partial frame across a missed deadline", "[knx][tcp][unit]")
    {
        // A receive that times out simply stops asking; what arrived stays until the rest of the frame follows.
        kd::frame_reassembler value {};
        const auto frame = detail::tunnelling_request();
        detail::feed(value, std::span {frame}.first(15u));
        CHECK(detail::waiting(value));
        CHECK(detail::waiting(value));
        detail::feed(value, std::span {frame}.subspan(15u));
        CHECK(detail::take(value) == frame);
    }

    TEST_CASE("knx frame reassembler refuses a header it cannot trust", "[knx][tcp][unit]")
    {
        const auto refused = [](const detail::octets_t& header, const error expected)
        {
            kd::frame_reassembler value {};
            detail::feed(value, header);
            const auto frame = value.next();
            return !frame.has_value() && (frame.error() == make_error_code(expected));
        };
        CHECK(refused({0x06u, 0x10u, 0x02u, 0x07u, 0xFFu, 0xFFu}, error::invalid_length));
        CHECK(refused({0x06u, 0x10u, 0x02u, 0x07u, 0x05u, 0xC1u}, error::invalid_length));
        CHECK(refused({0x06u, 0x10u, 0x02u, 0x07u, 0x00u, 0x05u}, error::malformed_frame));
        CHECK(refused({0x06u, 0x11u, 0x02u, 0x07u, 0x00u, 0x08u}, error::malformed_frame));
        CHECK(refused({0x05u, 0x10u, 0x02u, 0x07u, 0x00u, 0x08u}, error::malformed_frame));

        // The largest frame the buffer holds is accepted whole.
        kd::frame_reassembler value {};
        detail::octets_t largest(kd::frame_reassembler::capacity, 0u);
        std::ranges::copy(detail::octets_t {0x06u, 0x10u, 0x04u, 0x20u, 0x05u, 0xC0u}, largest.begin());
        detail::feed(value, largest);
        CHECK(detail::take(value).size() == kd::frame_reassembler::capacity);
    }

    TEST_CASE("knx frame reassembler makes room behind an unfinished frame and forgets on reset", "[knx][tcp][unit]")
    {
        kd::frame_reassembler value {};
        // A long run of small frames followed by the start of a large one: the large one still fits once the small
        // ones have been handed out, because what is left moves to the front.
        detail::octets_t stream {};
        for (std::uint8_t channel = 1u; channel <= 100u; ++channel)
        {
            const auto frame = detail::heartbeat(channel);
            stream.insert(stream.end(), frame.begin(), frame.end());
        }

        detail::feed(value, stream);
        for (std::uint8_t channel = 1u; channel <= 100u; ++channel)
            CHECK(detail::take(value) == detail::heartbeat(channel));

        detail::octets_t largest(kd::frame_reassembler::capacity, 0u);
        std::ranges::copy(detail::octets_t {0x06u, 0x10u, 0x04u, 0x20u, 0x05u, 0xC0u}, largest.begin());
        detail::feed(value, std::span {largest}.first(100u));
        CHECK(value.writable().size() == (kd::frame_reassembler::capacity - 100u));
        value.reset();
        CHECK(!value.partial());
        CHECK(value.writable().size() == kd::frame_reassembler::capacity);
    }
}
