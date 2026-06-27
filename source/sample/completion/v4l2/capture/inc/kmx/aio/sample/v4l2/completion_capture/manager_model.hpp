/// @file kmx/aio/sample/v4l2/completion_capture/manager_model.hpp
/// @brief Deterministic control-flow model for the completion V4L2 capture sample.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <cstdint>
#include <span>

namespace kmx::aio::sample::v4l2::completion_capture
{
    enum class startup_error : std::uint8_t
    {
        none = 0u,
        device_open,
        capture_create,
    };

    struct capture_frame_result
    {
        bool recv_ok {true};
        bool frame_ok {true};
        std::uint64_t bytes_used {};
    };

    struct capture_step_results
    {
        bool device_open_ok {};
        bool capture_create_ok {};
        bool timer_create_ok {};
        std::span<const capture_frame_result> frames {};
    };

    struct capture_simulation
    {
        startup_error error {};
        std::uint64_t frames_captured {};
        std::uint64_t bytes_captured {};
        std::uint64_t timer_ticks {};
        std::uint64_t errors {};
        bool capture_started {};
        bool timer_started {};
        bool shared_executor {};
    };

    [[nodiscard]] constexpr auto simulate_manager(const capture_step_results& step) noexcept -> capture_simulation
    {
        capture_simulation out {};

        if (!step.device_open_ok)
        {
            out.error = startup_error::device_open;
            out.errors = 1u;
            return out;
        }

        if (!step.capture_create_ok)
        {
            out.error = startup_error::capture_create;
            out.errors = 1u;
            return out;
        }

        out.capture_started = true;
        out.timer_started = step.timer_create_ok;
        out.shared_executor = out.capture_started && out.timer_started;

        if (!step.timer_create_ok)
        {
            out.errors = 1u;
            return out;
        }

        for (const auto& frame: step.frames)
        {
            if (!frame.recv_ok)
            {
                out.errors += 1u;
                continue;
            }

            if (!frame.frame_ok)
            {
                out.errors += 1u;
                continue;
            }

            out.frames_captured += 1u;
            out.bytes_captured += frame.bytes_used;
            out.timer_ticks += 1u;
        }

        return out;
    }
} // namespace kmx::aio::sample::v4l2::completion_capture