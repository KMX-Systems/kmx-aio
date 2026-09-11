/// @file src/kmx/aio/avb/gptp/generic_clock.cpp
/// @brief IEEE 802.1AS gPTP slave clock: definition of generic_clock, its state and its instantiations for both execution models.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/avb/gptp/generic_clock.hpp>
#ifndef PCH
    #include <kmx/aio/avb/avb_types.hpp>
    #include <kmx/aio/avb/generic_eth_socket.hpp>
    #include <kmx/aio/avb/gptp/messages.hpp>
    #include <kmx/aio/avb/gptp/primary_clock.hpp>
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/task.hpp>
    #include <kmx/logger.hpp>

    #include <atomic>
    #include <chrono>
    #include <cstdint>
    #include <expected>
    #include <memory>
    #include <source_location>
    #include <string_view>
    #include <system_error>
    #include <arpa/inet.h>
#endif

namespace kmx::aio::avb::gptp
{
    // state

    /// @brief Executor-specific part of the gPTP slave clock: the socket and the coroutine loops.
    template <typename Executor>
    struct generic_clock<Executor>::state: primary_clock
    {
        /// @brief The executor the clock's coroutines run on.
        Executor& exec_;
        /// @brief Raw Ethernet socket filtered to the gPTP EtherType.
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

        // Send a Pdelay_Req

        /// @brief Sends one Pdelay_Req and records its local TX time.
        /// @return Success or an error code.
        task_returning_expected_void_t send_pdelay_req() noexcept(false)
        {
            const auto buf = build_pdelay_req();
            co_return co_await sock_.send(multicast::gptp_peer, cspan_byte_t(buf));
        }

        // Main receive loop

        /// @brief Receives gPTP frames and dispatches them to the per-message handlers.
        /// @return An error code once the socket fails; never returns on success.
        task_returning_expected_void_t recv_loop() noexcept(false)
        {
            while (true)
            {
                auto res = co_await sock_.recv();
                if (!res)
                    co_return std::unexpected(res.error());

                const auto& [frame_bytes, hw_ts] = *res;
                dispatch(frame_bytes.data(), frame_bytes.size(), hw_ts);
            }
        }

        /// @brief Detachable wrapper around `recv_loop` that logs a terminal failure.
        task<void> recv_loop_task() noexcept(false)
        {
            const auto res = co_await recv_loop();
            if (!res)
                kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "gPTP receive loop failed: {}",
                                 res.error().message());
        }

        // Pdelay request loop (every ~1s by default)

        /// @brief Sends a Pdelay_Req once per second.
        /// @return An error code once a sleep or send fails; never returns on success.
        task_returning_expected_void_t pdelay_loop() noexcept(false)
        {
            while (true)
            {
                auto sleep_res = co_await sleep_for(std::chrono::seconds(1));
                if (!sleep_res)
                    co_return std::unexpected(sleep_res.error());

                auto send_res = co_await send_pdelay_req();
                if (!send_res)
                    co_return std::unexpected(send_res.error());
            }
        }

        /// @brief Detachable wrapper around `pdelay_loop` that logs a terminal failure.
        task<void> pdelay_loop_task() noexcept(false)
        {
            const auto res = co_await pdelay_loop();
            if (!res)
                kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "gPTP pdelay loop failed: {}",
                                 res.error().message());
        }
    };

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
    tai_timestamp_t generic_clock<Executor>::now() const noexcept
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

    // Explicit instantiations, one per execution model; the model namespaces only alias them.

#if defined(KMX_AIO_FEATURE_READINESS)
    template class generic_clock<kmx::aio::readiness::executor>;
#endif

#if defined(KMX_AIO_FEATURE_COMPLETION)
    template class generic_clock<kmx::aio::completion::executor>;
#endif
}
