/// @file v4l2/completion_capture_model_test.cpp
/// @brief Deterministic tests for the completion V4L2 capture control flow model.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <array>

#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/sample/v4l2/completion_capture/manager_model.hpp>

using kmx::aio::sample::v4l2::completion_capture::capture_frame_result;
using kmx::aio::sample::v4l2::completion_capture::capture_step_results;
using kmx::aio::sample::v4l2::completion_capture::simulate_manager;
using kmx::aio::sample::v4l2::completion_capture::startup_error;

TEST_CASE("completion v4l2 model stops at device open failure", "[v4l2][completion][model]")
{
    capture_step_results step {};
    step.device_open_ok = false;

    const auto out = simulate_manager(step);
    REQUIRE(out.error == startup_error::device_open);
    REQUIRE(out.errors == 1u);
    REQUIRE_FALSE(out.capture_started);
    REQUIRE_FALSE(out.timer_started);
    REQUIRE_FALSE(out.shared_executor);
}

TEST_CASE("completion v4l2 model reports shared executor when capture and timer start", "[v4l2][completion][model]")
{
    const std::array<capture_frame_result, 3u> frames {
        capture_frame_result {.recv_ok = true, .frame_ok = true, .bytes_used = 1024u},
        capture_frame_result {.recv_ok = true, .frame_ok = false, .bytes_used = 2048u},
        capture_frame_result {.recv_ok = false, .frame_ok = true, .bytes_used = 4096u},
    };

    capture_step_results step {};
    step.device_open_ok = true;
    step.capture_create_ok = true;
    step.timer_create_ok = true;
    step.frames = frames;

    const auto out = simulate_manager(step);
    REQUIRE(out.error == startup_error::none);
    REQUIRE(out.errors == 2u);
    REQUIRE(out.capture_started);
    REQUIRE(out.timer_started);
    REQUIRE(out.shared_executor);
    REQUIRE(out.frames_captured == 1u);
    REQUIRE(out.bytes_captured == 1024u);
    REQUIRE(out.timer_ticks == 1u);
}

TEST_CASE("completion v4l2 model stops when timer creation fails", "[v4l2][completion][model]")
{
    capture_step_results step {};
    step.device_open_ok = true;
    step.capture_create_ok = true;
    step.timer_create_ok = false;

    const auto out = simulate_manager(step);
    REQUIRE(out.error == startup_error::none);
    REQUIRE(out.errors == 1u);
    REQUIRE(out.capture_started);
    REQUIRE_FALSE(out.timer_started);
    REQUIRE_FALSE(out.shared_executor);
    REQUIRE(out.frames_captured == 0u);
    REQUIRE(out.timer_ticks == 0u);
}