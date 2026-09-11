/// @file inc/kmx/aio/avb/gptp/primary_clock.hpp
/// @brief Executor-agnostic implementation state of the IEEE 802.1AS gPTP slave clock.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/avb/avb_types.hpp>
    #include <kmx/aio/avb/gptp/header.hpp>
    #include <kmx/aio/avb/gptp/pi_servo.hpp>

    #include <atomic>
    #include <cstddef>
    #include <cstdint>
    #include <optional>
    #include <vector>
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
        tai_timestamp_t t2_sync_recv_ {}; ///< local RX HW timestamp of Sync

        // Pdelay state

        /// @brief Sequence id of the next Pdelay_Req to send.
        std::uint16_t pdelay_seq_id_ {};
        tai_timestamp_t t1_pdelay_req_ {}; ///< local TX time of Pdelay_Req
        tai_timestamp_t t4_pdelay_res_ {}; ///< local RX time of Pdelay_Resp
        tai_timestamp_t t2_remote_ {};     ///< remote RX of our Pdelay_Req
        tai_timestamp_t t3_remote_ {};     ///< remote TX of Pdelay_Resp
        std::int64_t mean_path_delay_ {};  ///< smoothed one-way delay (ns)

        // Synchronisation gate — set once servo reaches lock

        /// @brief Set once the servo reports lock; read by `is_synced` and `wait_sync`.
        std::atomic<bool> synced_ {};

        // Read current CLOCK_TAI

        /// @brief Reads `CLOCK_TAI`.
        /// @return The current TAI time in nanoseconds.
        [[nodiscard]] static tai_timestamp_t clock_tai_now() noexcept;

        // Build a minimal gPTP header

        /// @brief Builds a minimal gPTP header for an outgoing message.
        /// @param t      Message type.
        /// @param len    Total message length in bytes.
        /// @param seq_id Sequence id to stamp.
        /// @return The populated header in network byte order.
        [[nodiscard]] header make_header(msg_type t, std::uint16_t len, std::uint16_t seq_id) const noexcept;

        /// @brief Encodes the next Pdelay_Req frame and records its local TX time.
        /// @return The frame bytes ready to hand to the socket.
        [[nodiscard]] std::vector<std::byte> build_pdelay_req() noexcept;

        // Frame dispatch

        /// @brief Routes one received gPTP frame to the matching handler.
        /// @param data     Frame bytes.
        /// @param len      Frame length; frames shorter than a header are dropped.
        /// @param rx_hw_ts Hardware RX timestamp of the frame.
        void dispatch(const std::byte* data, std::size_t len, tai_timestamp_t rx_hw_ts) noexcept;

        /// @brief Records the arrival of a Sync from the tracked grandmaster.
        /// @param data     Frame bytes.
        /// @param len      Frame length.
        /// @param rx_hw_ts Hardware RX timestamp of the frame.
        void on_sync(const std::byte* data, std::size_t len, tai_timestamp_t rx_hw_ts) noexcept;

        /// @brief Applies a Follow_Up's precise origin timestamp to the servo.
        /// @param data Frame bytes.
        /// @param len  Frame length.
        void on_follow_up(const std::byte* data, std::size_t len) noexcept;

        /// @brief Records t4 and the remote t2 from a Pdelay_Resp addressed to us.
        /// @param data     Frame bytes.
        /// @param len      Frame length.
        /// @param rx_hw_ts Hardware RX timestamp of the frame.
        void on_pdelay_resp(const std::byte* data, std::size_t len, tai_timestamp_t rx_hw_ts) noexcept;

        /// @brief Completes the peer-delay exchange and updates the smoothed path delay.
        /// @param data Frame bytes.
        /// @param len  Frame length.
        void on_pdelay_resp_follow_up(const std::byte* data, std::size_t len) noexcept;

        /// @brief Latches the grandmaster clock identity from the first Announce seen.
        /// @param data Frame bytes.
        /// @param len  Frame length.
        void on_announce(const std::byte* data, std::size_t len) noexcept;
    };
}
