/// @file avb/gptp/clock_state.hpp
/// @brief Private implementation state of the IEEE 802.1AS gPTP slave clock.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <atomic>
    #include <chrono>
    #include <cstdint>
    #include <optional>
    #include <vector>

    #include <kmx/aio/avb/eth_socket.hpp>
    #include <kmx/aio/avb/gptp/clock.hpp>
    #include <kmx/aio/avb/gptp/messages.hpp>
    #include <kmx/aio/avb/gptp/servo.hpp>
    #include <kmx/logger.hpp>
#endif

namespace kmx::aio::avb::gptp
{
    /// @brief Executor-agnostic part of the gPTP slave clock: servo, timestamps and message decoding.
    /// @details Holds everything that does not depend on the executor type, so the code is emitted once
    ///          instead of once per pillar. `generic_clock<Executor>::state` adds the socket and the
    ///          executor-specific coroutine loops.
    struct primary_clock
    {
        /// @brief PI servo disciplining the local clock.
        pi_servo servo_ {};
        /// @brief Port identity derived from the NIC MAC.
        port_identity_t local_port_id_ {};

        // Grandmaster tracking

        /// @brief Clock identity of the tracked grandmaster, once one is seen.
        std::optional<clock_identity_t> gm_id_ {};

        // Sync state

        /// @brief Sequence id of the last accepted Sync.
        std::uint16_t sync_seq_id_ {};
        avb_timestamp_t t2_sync_recv_ {}; ///< local RX HW timestamp of Sync

        // Pdelay state

        /// @brief Sequence id of the next Pdelay_Req to send.
        std::uint16_t pdelay_seq_id_ {};
        avb_timestamp_t t1_pdelay_req_ {}; ///< local TX time of Pdelay_Req
        avb_timestamp_t t4_pdelay_res_ {}; ///< local RX time of Pdelay_Resp
        avb_timestamp_t t2_remote_ {};     ///< remote RX of our Pdelay_Req
        avb_timestamp_t t3_remote_ {};     ///< remote TX of Pdelay_Resp
        std::int64_t mean_path_delay_ {};  ///< smoothed one-way delay (ns)

        // Synchronisation gate — set once servo reaches lock

        /// @brief Set once the servo reports lock; read by `is_synced` and `wait_sync`.
        std::atomic<bool> synced_ {};

        // Read current CLOCK_TAI

        /// @brief Reads `CLOCK_TAI`.
        /// @return The current TAI time in nanoseconds.
        [[nodiscard]] static avb_timestamp_t clock_tai_now() noexcept;

        // Build a minimal gPTP header

        /// @brief Builds a minimal gPTP header for an outgoing message.
        /// @param t      Message type.
        /// @param len    Total message length in bytes.
        /// @param seq_id Sequence id to stamp.
        /// @return The populated header in network byte order.
        [[nodiscard]] header_t make_header(msg_type t, std::uint16_t len, std::uint16_t seq_id) const noexcept;

        /// @brief Encodes the next Pdelay_Req frame and records its local TX time.
        /// @return The frame bytes ready to hand to the socket.
        [[nodiscard]] std::vector<std::byte> build_pdelay_req() noexcept;

        // Frame dispatch

        /// @brief Routes one received gPTP frame to the matching handler.
        /// @param data     Frame bytes.
        /// @param len      Frame length; frames shorter than a header are dropped.
        /// @param rx_hw_ts Hardware RX timestamp of the frame.
        void dispatch(const std::byte* data, std::size_t len, avb_timestamp_t rx_hw_ts) noexcept;

        /// @brief Records the arrival of a Sync from the tracked grandmaster.
        /// @param data     Frame bytes.
        /// @param len      Frame length.
        /// @param rx_hw_ts Hardware RX timestamp of the frame.
        void on_sync(const std::byte* data, std::size_t len, avb_timestamp_t rx_hw_ts) noexcept;

        /// @brief Applies a Follow_Up's precise origin timestamp to the servo.
        /// @param data Frame bytes.
        /// @param len  Frame length.
        void on_follow_up(const std::byte* data, std::size_t len) noexcept;

        /// @brief Records t4 and the remote t2 from a Pdelay_Resp addressed to us.
        /// @param data     Frame bytes.
        /// @param len      Frame length.
        /// @param rx_hw_ts Hardware RX timestamp of the frame.
        void on_pdelay_resp(const std::byte* data, std::size_t len, avb_timestamp_t rx_hw_ts) noexcept;

        /// @brief Completes the peer-delay exchange and updates the smoothed path delay.
        /// @param data Frame bytes.
        /// @param len  Frame length.
        void on_pdelay_resp_follow_up(const std::byte* data, std::size_t len) noexcept;

        /// @brief Latches the grandmaster clock identity from the first Announce seen.
        /// @param data Frame bytes.
        /// @param len  Frame length.
        void on_announce(const std::byte* data, std::size_t len) noexcept;
    };

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
            {
                kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "gPTP receive loop failed: {}",
                                 res.error().message());
            }
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
}
