/// @file kmx/aio/sample/avb/manager_model.hpp
/// @brief Deterministic AVB manager control-flow model for unit tests and helper math.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <cstdint>
#include <span>

namespace kmx::aio::sample::avb
{
    enum class startup_error : std::uint8_t
    {
        none = 0u,
        clock_start,
        clock_sync,
        socket_open,
        srp_start,
        srp_advertise,
        srp_subscribe,
        srp_withdraw,
    };

    struct talker_step_results
    {
        bool clock_start_ok {true};
        bool clock_sync_ok {true};
        bool socket_open_ok {true};
        bool srp_start_ok {true};
        bool srp_advertise_ok {true};
        bool srp_withdraw_ok {true};
        bool diagnostics_only {};
        std::uint64_t send_attempts {};
        std::uint64_t send_failures {};
    };

    struct talker_simulation
    {
        startup_error error {startup_error::none};
        std::uint64_t frames_sent {};
        std::uint64_t errors {};
        bool streamed_payload {};
        bool withdrew_stream {};
    };

    [[nodiscard]] constexpr auto simulate_talker_manager(const talker_step_results& step) noexcept -> talker_simulation
    {
        talker_simulation out {};

        if (!step.clock_start_ok)
        {
            out.error = startup_error::clock_start;
            out.errors = 1u;
            return out;
        }
        if (!step.clock_sync_ok)
        {
            out.error = startup_error::clock_sync;
            out.errors = 1u;
            return out;
        }
        if (!step.socket_open_ok)
        {
            out.error = startup_error::socket_open;
            out.errors = 1u;
            return out;
        }
        if (!step.srp_start_ok)
        {
            out.error = startup_error::srp_start;
            out.errors = 1u;
            return out;
        }
        if (!step.srp_advertise_ok)
        {
            out.error = startup_error::srp_advertise;
            out.errors = 1u;
            return out;
        }

        out.streamed_payload = !step.diagnostics_only;
        if (!step.diagnostics_only)
        {
            const auto failures = (step.send_failures > step.send_attempts) ? step.send_attempts : step.send_failures;
            out.frames_sent = step.send_attempts - failures;
            out.errors += failures;
        }

        out.withdrew_stream = step.srp_withdraw_ok;
        if (!step.srp_withdraw_ok)
        {
            out.error = startup_error::srp_withdraw;
            out.errors += 1u;
        }

        return out;
    }

    [[nodiscard]] constexpr auto abs_diff_u64(const std::uint64_t lhs, const std::uint64_t rhs) noexcept -> std::uint64_t
    {
        return (lhs >= rhs) ? (lhs - rhs) : (rhs - lhs);
    }

    struct listener_frame_result
    {
        bool recv_ok {true};
        bool parse_ok {true};
        bool stream_match {true};
        std::uint64_t rx_ts {0u};
        std::uint64_t presentation_ts {0u};
    };

    struct listener_step_results
    {
        bool clock_start_ok {true};
        bool clock_sync_ok {true};
        bool socket_open_ok {true};
        bool srp_start_ok {true};
        bool srp_subscribe_ok {true};
        bool srp_withdraw_ok {true};
        bool diagnostics_only {false};
        std::span<const listener_frame_result> frames {};
    };

    struct listener_simulation
    {
        startup_error error {startup_error::none};
        std::uint64_t frames_received {0u};
        std::uint64_t frames_parsed {0u};
        std::uint64_t jitter_abs_sum_ns {0u};
        std::uint64_t jitter_abs_max_ns {0u};
        std::uint64_t errors {0u};
        bool withdrew_stream {false};
    };

    [[nodiscard]] constexpr auto simulate_listener_manager(const listener_step_results& step) noexcept -> listener_simulation
    {
        listener_simulation out {};

        if (!step.clock_start_ok)
        {
            out.error = startup_error::clock_start;
            out.errors = 1u;
            return out;
        }
        if (!step.clock_sync_ok)
        {
            out.error = startup_error::clock_sync;
            out.errors = 1u;
            return out;
        }
        if (!step.socket_open_ok)
        {
            out.error = startup_error::socket_open;
            out.errors = 1u;
            return out;
        }
        if (!step.srp_start_ok)
        {
            out.error = startup_error::srp_start;
            out.errors = 1u;
            return out;
        }
        if (!step.srp_subscribe_ok)
        {
            out.error = startup_error::srp_subscribe;
            out.errors = 1u;
            return out;
        }

        if (!step.diagnostics_only)
        {
            for (const auto& frame: step.frames)
            {
                if (!frame.recv_ok)
                {
                    out.errors += 1u;
                    continue;
                }

                out.frames_received += 1u;

                if (!frame.parse_ok)
                {
                    out.errors += 1u;
                    continue;
                }

                if (!frame.stream_match)
                    continue;

                out.frames_parsed += 1u;

                const auto jitter = abs_diff_u64(frame.rx_ts, frame.presentation_ts);
                out.jitter_abs_sum_ns += jitter;
                if (jitter > out.jitter_abs_max_ns)
                    out.jitter_abs_max_ns = jitter;
            }
        }

        out.withdrew_stream = step.srp_withdraw_ok;
        if (!step.srp_withdraw_ok)
        {
            out.error = startup_error::srp_withdraw;
            out.errors += 1u;
        }

        return out;
    }
} // namespace kmx::aio::sample::avb
