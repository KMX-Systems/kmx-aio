/// @file kmx/aio/sample/avb/talker/manager.hpp
/// @brief Completion-model AVB talker sample manager.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

#include <kmx/aio/avb/avb_types.hpp>
#include <kmx/aio/completion/avb/gptp/clock.hpp>
#include <kmx/aio/completion/avb/srp/client.hpp>
#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::sample::avb::talker
{
    struct config
    {
        std::string iface {"eth0"};
        kmx::aio::avb::mac_address_t dest_mac {0x91u, 0xE0u, 0xF0u, 0x00u, 0x0Eu, 0x80u};
        std::uint16_t stream_unique_id {1u};
        std::uint64_t max_frames {4000u};
        std::uint32_t payload_bytes {48u};
        std::chrono::microseconds frame_period {125u};
        std::chrono::seconds sync_timeout {5u};
        bool diagnostics_only {false};
    };

    struct metrics
    {
        std::atomic_uint64_t frames_sent {};
        std::atomic_uint64_t errors {};
    };

    class manager
    {
    public:
        explicit manager(config cfg = {}): config_(std::move(cfg)) {}
        [[nodiscard]] bool run() noexcept(false);

    private:
        [[nodiscard]] kmx::aio::task<void> talker_loop() noexcept(false);
        [[nodiscard]] kmx::aio::task<void> stats_loop() noexcept(false);
        void print_statistics() const;
        static void signal_handler(int signum) noexcept;

        config config_ {};
        metrics metrics_ {};

        std::unique_ptr<kmx::aio::completion::executor> executor_ {};
        std::unique_ptr<kmx::aio::completion::avb::gptp::clock> clock_ {};
        std::unique_ptr<kmx::aio::completion::avb::srp::client> srp_ {};
        static inline std::atomic<kmx::aio::completion::executor*> g_executor_ptr {};
    };
} // namespace kmx::aio::sample::avb::talker
