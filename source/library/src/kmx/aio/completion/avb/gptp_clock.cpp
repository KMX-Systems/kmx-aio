/// @file completion/avb/gptp_clock.cpp
/// @brief IEEE 802.1AS gPTP slave state machine implementation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <kmx/aio/avb/gptp/clock_state.hpp>
#include <kmx/aio/completion/executor.hpp>

namespace kmx::aio::avb::gptp
{
    // generic_clock API

    template <typename Executor>
    generic_clock<Executor>::generic_clock(Executor& exec) noexcept: state_(std::make_unique<state>(exec))
    {
    }

    template <typename Executor>
    generic_clock<Executor>::~generic_clock() noexcept = default;

    template <typename Executor>
    task_returning_expected_void_t generic_clock<Executor>::start(const std::string_view iface) noexcept(false)
    {
        // Open raw Ethernet socket filtered to gPTP EtherType
        auto open_res = co_await state_->sock_.open(iface, ethertype::gptp);
        if (!open_res)
            co_return std::unexpected(open_res.error());

        // Derive local port identity from NIC MAC
        auto& local_port_id = state_->local_port_id_;
        local_port_id.clock_id = mac_to_clock_id(state_->sock_.local_mac());
        local_port_id.port_number = ::htons(1u);

        // Spawn receive loop and pdelay loop as detached tasks on the executor
        auto& exec = state_->exec_;
        exec.spawn(state_->recv_loop_task());
        exec.spawn(state_->pdelay_loop_task());

        co_return expected_void_t {};
    }

    template <typename Executor>
    avb_timestamp_t generic_clock<Executor>::now() const noexcept
    {
        return state::clock_tai_now();
    }

    template <typename Executor>
    task_returning_expected_void_t generic_clock<Executor>::wait_sync(std::chrono::milliseconds timeout) noexcept(false)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;

        while (!state_->synced_.load(std::memory_order_acquire))
        {
            if (std::chrono::steady_clock::now() >= deadline)
                co_return std::unexpected(std::make_error_code(std::errc::timed_out));

            const auto sleep_res = co_await state_->sleep_for(std::chrono::milliseconds(50));
            if (!sleep_res)
                co_return std::unexpected(sleep_res.error());
        }

        co_return expected_void_t {};
    }

    template <typename Executor>
    std::int64_t generic_clock<Executor>::offset_ns() const noexcept
    {
        return state_->servo_.last_offset();
    }

    template <typename Executor>
    std::int64_t generic_clock<Executor>::path_delay_ns() const noexcept
    {
        return state_->mean_path_delay_;
    }

    template <typename Executor>
    bool generic_clock<Executor>::is_synced() const noexcept
    {
        return state_->synced_.load(std::memory_order_acquire);
    }

    // Explicit instantiation

    template class generic_clock<kmx::aio::completion::executor>;

} // namespace kmx::aio::avb::gptp
