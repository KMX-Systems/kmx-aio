/// @file src/kmx/aio/avb/srp/generic_client.cpp
/// @brief IEEE 802.1Qat SRP (MSRP) client: definition of generic_client, its state and its instantiations for both execution models.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/avb/srp/generic_client.hpp>
#ifndef PCH
    #include <kmx/aio/avb/avb_types.hpp>
    #include <kmx/aio/avb/generic_eth_socket.hpp>
    #include <kmx/aio/avb/srp/messages.hpp>
    #include <kmx/aio/avb/srp/primary_client.hpp>
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/task.hpp>
    #include <kmx/logger.hpp>

    #include <chrono>
    #include <cstdint>
    #include <expected>
    #include <memory>
    #include <source_location>
    #include <string_view>
    #include <system_error>
    #include <utility>
#endif

namespace kmx::aio::avb::srp
{
    // state

    /// @brief Executor-specific part of the SRP client: the socket and the coroutine loops.
    template <typename Executor>
    struct generic_client<Executor>::state: primary_client
    {
        /// @brief The executor the client's coroutines run on.
        Executor& exec_;
        /// @brief Raw Ethernet socket filtered to the MSRP EtherType.
        kmx::aio::avb::generic_eth_socket<Executor> sock_;

        /// @brief Creates the state bound to an executor.
        /// @param exec The executor the socket and coroutine loops use.
        explicit state(Executor& exec) noexcept: exec_(exec), sock_(exec) {}

        /// @brief Suspends the calling coroutine for the given duration.
        /// @param duration How long to sleep.
        /// @return Success once the timer fires, or an error code.
        template <typename Duration>
        [[nodiscard]] task_returning_expected_void_t sleep_for(Duration duration) noexcept(false)
        {
            static_assert(
                requires(Executor& e) { e.async_timeout(std::uint64_t {}); },
                "Executor must support async_timeout(std::uint64_t duration_ns)");
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
            co_return co_await exec_.async_timeout(static_cast<std::uint64_t>(ns.count()));
        }

        // Send helpers

        /// @brief Sends one MSRP Talker Advertise for a stream.
        /// @param desc The stream being advertised.
        /// @return Success or an error code.
        task_returning_expected_void_t send_talker_advertise(const stream_descriptor& desc) noexcept(false)
        {
            const auto buf = build_talker_advertise(desc);
            co_return co_await sock_.send(multicast::srp, cspan_byte_t(buf));
        }

        /// @brief Sends one MSRP Listener Ready declaration for a stream.
        /// @param desc The stream being subscribed to.
        /// @return Success or an error code.
        task_returning_expected_void_t send_listener_ready(const stream_descriptor& desc) noexcept(false)
        {
            const auto buf = build_listener_ready(desc);
            co_return co_await sock_.send(multicast::srp, cspan_byte_t(buf));
        }

        /// @brief Announces SR Class A domain support on the bound interface.
        /// @return Success or an error code.
        task_returning_expected_void_t send_domain() noexcept(false)
        {
            const auto buf = build_domain();
            co_return co_await sock_.send(multicast::srp, cspan_byte_t(buf));
        }

        // Receive loop

        /// @brief Receives MSRP frames and dispatches them to the per-attribute handlers.
        /// @return An error code once the socket fails; never returns on success.
        task_returning_expected_void_t recv_loop() noexcept(false)
        {
            while (true)
            {
                auto res = co_await sock_.recv();
                if (!res)
                    co_return std::unexpected(res.error());

                const auto& [frame_bytes, hw_ts] = *res;
                dispatch(frame_bytes.data(), frame_bytes.size());
            }
        }

        /// @brief Detachable wrapper around `recv_loop` that logs a terminal failure.
        task<void> recv_loop_task() noexcept(false)
        {
            const auto res = co_await recv_loop();
            if (!res)
                kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SRP receive loop failed: {}",
                                 res.error().message());
        }

        // Periodic re-declaration loop

        /// @brief Re-sends every talker and listener declaration twice a second.
        /// @return An error code once a sleep or send fails; never returns on success.
        task_returning_expected_void_t talker_loop() noexcept(false);

        /// @brief Detachable wrapper around `talker_loop` that logs a terminal failure.
        task<void> talker_loop_task() noexcept(false)
        {
            const auto res = co_await talker_loop();
            if (!res)
                kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SRP re-declaration loop failed: {}",
                                 res.error().message());
        }
    };

    template <typename Executor>
    task_returning_expected_void_t generic_client<Executor>::state::talker_loop() noexcept(false)
    {
        while (true)
        {
            const auto sleep = co_await sleep_for(std::chrono::milliseconds(500));
            if (!sleep)
                co_return std::unexpected(sleep.error());

            for (const auto& [id, desc]: talker_streams_)
            {
                auto s = co_await send_talker_advertise(desc);
                if (!s)
                    co_return std::unexpected(s.error());
            }

            for (const auto& [id, desc]: listener_streams_)
            {
                auto s = co_await send_listener_ready(desc);
                if (!s)
                    co_return std::unexpected(s.error());
            }
        }
    }

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
        static_cast<void>(inserted);

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

    // Explicit instantiations, one per execution model; the model namespaces only alias them.

#if defined(KMX_AIO_FEATURE_READINESS)
    template class generic_client<kmx::aio::readiness::executor>;
#endif

#if defined(KMX_AIO_FEATURE_COMPLETION)
    template class generic_client<kmx::aio::completion::executor>;
#endif
}
