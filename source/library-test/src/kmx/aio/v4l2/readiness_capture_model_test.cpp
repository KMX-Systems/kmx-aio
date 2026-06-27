/// @file v4l2/readiness_capture_model_test.cpp
/// @brief Deterministic tests for readiness/completion V4L2 model parity.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <array>

#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/sample/v4l2/capture/manager_model.hpp>
#include <kmx/aio/sample/v4l2/completion_capture/manager_model.hpp>

namespace readiness_v4l2 = kmx::aio::sample::v4l2::capture;
namespace completion_v4l2 = kmx::aio::sample::v4l2::completion_capture;

using readiness_v4l2::capture_frame_result;
using readiness_v4l2::capture_step_results;

TEST_CASE("readiness and completion v4l2 models agree on frame accounting", "[v4l2][readiness][completion][model]")
{
    static constexpr std::array<capture_frame_result, 4u> readiness_frames {
        capture_frame_result {.recv_ok = true, .frame_ok = true, .bytes_used = 512u},
        capture_frame_result {.recv_ok = true, .frame_ok = false, .bytes_used = 1024u},
        capture_frame_result {.recv_ok = false, .frame_ok = true, .bytes_used = 2048u},
        capture_frame_result {.recv_ok = true, .frame_ok = true, .bytes_used = 1536u},
    };

    static constexpr std::array<completion_v4l2::capture_frame_result, 4u> completion_frames {
        completion_v4l2::capture_frame_result {.recv_ok = true, .frame_ok = true, .bytes_used = 512u},
        completion_v4l2::capture_frame_result {.recv_ok = true, .frame_ok = false, .bytes_used = 1024u},
        completion_v4l2::capture_frame_result {.recv_ok = false, .frame_ok = true, .bytes_used = 2048u},
        completion_v4l2::capture_frame_result {.recv_ok = true, .frame_ok = true, .bytes_used = 1536u},
    };

    capture_step_results readiness_step {};
    readiness_step.device_open_ok = true;
    readiness_step.capture_create_ok = true;
    readiness_step.frames = readiness_frames;

    completion_v4l2::capture_step_results completion_step {};
    completion_step.device_open_ok = true;
    completion_step.capture_create_ok = true;
    completion_step.timer_create_ok = true;
    completion_step.frames = completion_frames;

    const auto readiness_out = readiness_v4l2::simulate_manager(readiness_step);
    const auto completion_out = completion_v4l2::simulate_manager(completion_step);

    REQUIRE(readiness_out.capture_started);
    REQUIRE(completion_out.capture_started);
    REQUIRE(completion_out.timer_started);
    REQUIRE(completion_out.shared_executor);
    REQUIRE(readiness_out.frames_captured == completion_out.frames_captured);
    REQUIRE(readiness_out.bytes_captured == completion_out.bytes_captured);
    REQUIRE(readiness_out.errors == completion_out.errors);
}

TEST_CASE("readiness and completion v4l2 models share startup failure behavior", "[v4l2][readiness][completion][model]")
{
    capture_step_results readiness_step {};
    readiness_step.device_open_ok = false;

    completion_v4l2::capture_step_results completion_step {};
    completion_step.device_open_ok = false;
    completion_step.capture_create_ok = true;
    completion_step.timer_create_ok = true;

    const auto readiness_out = readiness_v4l2::simulate_manager(readiness_step);
    const auto completion_out = completion_v4l2::simulate_manager(completion_step);

    REQUIRE(readiness_out.error == readiness_v4l2::startup_error::device_open);
    REQUIRE(completion_out.error == completion_v4l2::startup_error::device_open);
    REQUIRE(readiness_out.errors == 1u);
    REQUIRE(completion_out.errors == 1u);
    REQUIRE_FALSE(readiness_out.capture_started);
    REQUIRE_FALSE(completion_out.capture_started);
}