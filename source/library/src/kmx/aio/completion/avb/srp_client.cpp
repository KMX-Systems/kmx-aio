/// @file completion/avb/srp_client.cpp
/// @brief IEEE 802.1Qat SRP (MSRP) talker and listener state machine.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <kmx/aio/avb/srp/client_state.hpp>
#include <kmx/aio/completion/executor.hpp>

namespace kmx::aio::avb::srp
{
    // generic_client API

    template <typename Executor>
    generic_client<Executor>::generic_client(Executor& exec) noexcept: state_(std::make_unique<state>(exec))
    {
    }

    template <typename Executor>
    generic_client<Executor>::~generic_client() noexcept = default;

    template <typename Executor>
    task_returning_expected_void_t generic_client<Executor>::start(const std::string_view iface) noexcept(false)
    {
        const auto open_res = co_await state_->sock_.open(iface, ethertype::msrp);
        if (!open_res)
            co_return std::unexpected(open_res.error());

        // Announce domain support first
        const auto dom = co_await state_->send_domain();
        if (!dom)
            co_return std::unexpected(dom.error());

        auto& exec = state_->exec_;
        exec.spawn(state_->recv_loop_task());
        exec.spawn(state_->talker_loop_task());

        co_return expected_void_t {};
    }

    template <typename Executor>
    task_returning_expected_void_t generic_client<Executor>::advertise(const stream_descriptor& desc) noexcept(false)
    {
        state_->talker_streams_[desc.stream_id] = desc;
        co_return co_await state_->send_talker_advertise(desc);
    }

    template <typename Executor>
    task<std::expected<stream_descriptor, std::error_code>> generic_client<Executor>::subscribe(
        const stream_id_t& stream_id, std::chrono::milliseconds timeout) noexcept(false)
    {
        // Register a waiter entry.
        typename state::sub_waiter waiter_entry {stream_id, {}};
        auto [waiter, inserted] = state_->pending_subs_.insert_or_assign(stream_id, std::move(waiter_entry));
        (void) inserted;

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!waiter->second.resolved.has_value())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                state_->pending_subs_.erase(stream_id);
                co_return std::unexpected(std::make_error_code(std::errc::timed_out));
            }

            auto sleep = co_await state_->sleep_for(std::chrono::milliseconds(50));
            if (!sleep)
                co_return std::unexpected(sleep.error());
        }

        const stream_descriptor desc = *waiter->second.resolved;
        state_->listener_streams_[stream_id] = desc;

        // Remove waiter
        state_->pending_subs_.erase(stream_id);

        // Send initial Listener Ready
        auto send_res = co_await state_->send_listener_ready(desc);
        if (!send_res)
            co_return std::unexpected(send_res.error());

        co_return desc;
    }

    template <typename Executor>
    task_returning_expected_void_t generic_client<Executor>::withdraw(const stream_id_t& stream_id) noexcept(false)
    {
        state_->talker_streams_.erase(stream_id);
        state_->listener_streams_.erase(stream_id);
        co_return expected_void_t {};
    }

    // Explicit instantiation

    template class generic_client<kmx::aio::completion::executor>;

} // namespace kmx::aio::avb::srp
