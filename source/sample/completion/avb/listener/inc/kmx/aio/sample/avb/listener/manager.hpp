/// @file kmx/aio/sample/avb/listener/manager.hpp
/// @brief Completion-model AVB listener sample manager.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

#include <kmx/aio/avb/avb_types.hpp>
#include <kmx/aio/avb/gptp/clock.hpp>
#include <kmx/aio/avb/srp/client.hpp>
#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::sample::avb::listener
{
    struct config
    {
        std::string iface {"eth0"};
        kmx::aio::avb::mac_address_t talker_mac {0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u};
        std::uint16_t stream_unique_id {1u};
        std::uint64_t max_frames {4000u};
        std::chrono::microseconds expected_period {125u};
        std::chrono::seconds sync_timeout {5u};
        std::chrono::seconds srp_subscribe_timeout {5u};
        bool diagnostics_only {false};
    };

    struct metrics
    {
        std::atomic_uint64_t frames_received {};
        std::atomic_uint64_t frames_parsed {};
        std::atomic_uint64_t errors {};
        std::atomic_uint64_t jitter_abs_sum_ns {};
        std::atomic_uint64_t jitter_abs_max_ns {};
    };

    class manager
    {
    public:
        explicit manager(config cfg = {}): config_(std::move(cfg)) {}
        [[nodiscard]] bool run() noexcept(false);

    private:
        [[nodiscard]] kmx::aio::task<void> receive_loop() noexcept(false);
        [[nodiscard]] kmx::aio::task<void> stats_loop() noexcept(false);
        void print_statistics() const;
        static void signal_handler(int signum) noexcept;

        config config_ {};
        metrics metrics_ {};

        std::shared_ptr<kmx::aio::completion::executor> executor_ {};
        std::unique_ptr<kmx::aio::completion::avb::gptp::clock> clock_ {};
        std::unique_ptr<kmx::aio::completion::avb::srp::client> srp_ {};
        static inline std::atomic<kmx::aio::completion::executor*> g_executor_ptr {};
    };
} // namespace kmx::aio::sample::avb::listener
