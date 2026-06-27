/// @file avb/avtp/am824_test.cpp
/// @brief Unit tests for AVTP AAF/AM824 framing helpers.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/avb/avtp/am824.hpp>

using kmx::aio::avb::avb_timestamp_t;
using kmx::aio::avb::stream_id_t;
using kmx::aio::avb::avtp::build_am824_frame;
using kmx::aio::avb::avtp::expand_avtp_timestamp_32;
using kmx::aio::avb::avtp::header_size;
using kmx::aio::avb::avtp::parse_am824_frame;
using kmx::aio::avb::avtp::subtype_aaf;
using kmx::aio::avb::avtp::to_avtp_timestamp_32;

TEST_CASE("avtp am824 build and parse roundtrip", "[avb][avtp][am824]")
{
    stream_id_t sid {};
    sid.source_mac = {0x02u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u};
    sid.unique_id = 0x1234u;

    const std::array<std::byte, 8u> payload {
        std::byte {0x10u}, std::byte {0x20u}, std::byte {0x30u}, std::byte {0x40u},
        std::byte {0x50u}, std::byte {0x60u}, std::byte {0x70u}, std::byte {0x80u},
    };

    const std::uint8_t seq = 77u;
    const avb_timestamp_t presentation_ns = 0x1234'5678'9ABC'DEF0ULL;

    const auto frame_res = build_am824_frame(sid, seq, presentation_ns, std::span<const std::byte>(payload));
    REQUIRE(frame_res.has_value());

    const auto parse_res = parse_am824_frame(std::span<const std::byte>(*frame_res));
    REQUIRE(parse_res.has_value());

    const auto& parsed = *parse_res;
    REQUIRE(parsed.stream_id.source_mac == sid.source_mac);
    REQUIRE(parsed.stream_id.unique_id == sid.unique_id);
    REQUIRE(parsed.sequence_num == seq);
    REQUIRE(parsed.avtp_timestamp_32 == to_avtp_timestamp_32(presentation_ns));
    REQUIRE(parsed.payload.size() == payload.size());

    for (std::size_t i = 0u; i < payload.size(); ++i)
        REQUIRE(parsed.payload[i] == payload[i]);
}

TEST_CASE("avtp am824 rejects payload larger than 16-bit length", "[avb][avtp][am824]")
{
    stream_id_t sid {};
    sid.source_mac = {0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u};
    sid.unique_id = 1u;

    std::vector<std::byte> payload(0x1'0000u, std::byte {0x7Fu});
    const auto frame_res = build_am824_frame(sid, 0u, 123u, std::span<const std::byte>(payload));
    REQUIRE_FALSE(frame_res.has_value());
    REQUIRE(frame_res.error().value() == EINVAL);
}

TEST_CASE("avtp am824 parse validates subtype and frame size", "[avb][avtp][am824]")
{
    const std::array<std::byte, header_size - 1u> short_frame {};
    const auto short_res = parse_am824_frame(std::span<const std::byte>(short_frame));
    REQUIRE_FALSE(short_res.has_value());
    REQUIRE(short_res.error().value() == EINVAL);

    std::array<std::byte, header_size> wrong_subtype {};
    wrong_subtype[0] = std::byte {0x7Fu};
    const auto subtype_res = parse_am824_frame(std::span<const std::byte>(wrong_subtype));
    REQUIRE_FALSE(subtype_res.has_value());
    REQUIRE(subtype_res.error().value() == EPROTO);

    std::array<std::byte, header_size> wrong_len {};
    wrong_len[0] = static_cast<std::byte>(subtype_aaf);
    wrong_len[16] = std::byte {0x00u};
    wrong_len[17] = std::byte {0x10u};
    const auto len_res = parse_am824_frame(std::span<const std::byte>(wrong_len));
    REQUIRE_FALSE(len_res.has_value());
    REQUIRE(len_res.error().value() == EMSGSIZE);
}

TEST_CASE("avtp timestamp expansion handles nearby 32-bit wrap", "[avb][avtp][am824]")
{
    const avb_timestamp_t ref_forward = 0x0000'0001'FFFF'FFF0ULL;
    const auto expanded_forward = expand_avtp_timestamp_32(0x0000'0020u, ref_forward);
    REQUIRE(expanded_forward == 0x0000'0002'0000'0020ULL);

    const avb_timestamp_t ref_backward = 0x0000'0002'0000'0010ULL;
    const auto expanded_backward = expand_avtp_timestamp_32(0xFFFF'FFF0u, ref_backward);
    REQUIRE(expanded_backward == 0x0000'0001'FFFF'FFF0ULL);
}