/// @file avb/manager_model_test.cpp
/// @brief Deterministic manager-model tests for AVB talker/listener control flow.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <array>

#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/sample/avb/manager_model.hpp>

using kmx::aio::sample::avb::abs_diff_u64;
using kmx::aio::sample::avb::listener_frame_result;
using kmx::aio::sample::avb::listener_step_results;
using kmx::aio::sample::avb::simulate_listener_manager;
using kmx::aio::sample::avb::simulate_talker_manager;
using kmx::aio::sample::avb::startup_error;
using kmx::aio::sample::avb::talker_step_results;

TEST_CASE("talker model stops at first startup failure", "[avb][talker][model]")
{
    talker_step_results step {};
    step.clock_sync_ok = false;
    step.send_attempts = 100u;

    const auto out = simulate_talker_manager(step);
    REQUIRE(out.error == startup_error::clock_sync);
    REQUIRE(out.errors == 1u);
    REQUIRE(out.frames_sent == 0u);
    REQUIRE_FALSE(out.streamed_payload);
    REQUIRE_FALSE(out.withdrew_stream);
}

TEST_CASE("talker model supports diagnostics-only run", "[avb][talker][model]")
{
    talker_step_results step {};
    step.diagnostics_only = true;
    step.send_attempts = 100u;

    const auto out = simulate_talker_manager(step);
    REQUIRE(out.error == startup_error::none);
    REQUIRE(out.errors == 0u);
    REQUIRE(out.frames_sent == 0u);
    REQUIRE_FALSE(out.streamed_payload);
    REQUIRE(out.withdrew_stream);
}

TEST_CASE("talker model counts send failures and withdraw error", "[avb][talker][model]")
{
    talker_step_results step {};
    step.send_attempts = 7u;
    step.send_failures = 2u;
    step.srp_withdraw_ok = false;

    const auto out = simulate_talker_manager(step);
    REQUIRE(out.error == startup_error::srp_withdraw);
    REQUIRE(out.frames_sent == 5u);
    REQUIRE(out.errors == 3u);
    REQUIRE(out.streamed_payload);
    REQUIRE_FALSE(out.withdrew_stream);
}

TEST_CASE("listener model tracks parsed frames and jitter with stream filtering", "[avb][listener][model]")
{
    const std::array<listener_frame_result, 4u> frames {
        listener_frame_result {.recv_ok = true, .parse_ok = true, .stream_match = true, .rx_ts = 1'010u, .presentation_ts = 1'000u},
        listener_frame_result {.recv_ok = true, .parse_ok = true, .stream_match = false, .rx_ts = 900u, .presentation_ts = 1'000u},
        listener_frame_result {.recv_ok = true, .parse_ok = false, .stream_match = true, .rx_ts = 1'050u, .presentation_ts = 1'000u},
        listener_frame_result {.recv_ok = true, .parse_ok = true, .stream_match = true, .rx_ts = 980u, .presentation_ts = 1'000u},
    };

    listener_step_results step {};
    step.frames = frames;

    const auto out = simulate_listener_manager(step);
    REQUIRE(out.error == startup_error::none);
    REQUIRE(out.frames_received == 4u);
    REQUIRE(out.frames_parsed == 2u);
    REQUIRE(out.jitter_abs_sum_ns == 30u);
    REQUIRE(out.jitter_abs_max_ns == 20u);
    REQUIRE(out.errors == 1u);
    REQUIRE(out.withdrew_stream);
}

TEST_CASE("listener model supports diagnostics-only run", "[avb][listener][model]")
{
    const std::array<listener_frame_result, 2u> frames {
        listener_frame_result {.recv_ok = true, .parse_ok = true, .stream_match = true, .rx_ts = 10u, .presentation_ts = 10u},
        listener_frame_result {.recv_ok = true, .parse_ok = true, .stream_match = true, .rx_ts = 12u, .presentation_ts = 11u},
    };

    listener_step_results step {};
    step.diagnostics_only = true;
    step.frames = frames;

    const auto out = simulate_listener_manager(step);
    REQUIRE(out.error == startup_error::none);
    REQUIRE(out.errors == 0u);
    REQUIRE(out.frames_received == 0u);
    REQUIRE(out.frames_parsed == 0u);
    REQUIRE(out.jitter_abs_sum_ns == 0u);
    REQUIRE(out.jitter_abs_max_ns == 0u);
    REQUIRE(out.withdrew_stream);
}

TEST_CASE("abs_diff helper is symmetric", "[avb][model][math]")
{
    REQUIRE(abs_diff_u64(10u, 42u) == 32u);
    REQUIRE(abs_diff_u64(42u, 10u) == 32u);
    REQUIRE(abs_diff_u64(7u, 7u) == 0u);
}
