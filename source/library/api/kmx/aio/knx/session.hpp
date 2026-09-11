/// @file api/kmx/aio/knx/session.hpp
/// @brief The timing and retry policy of a KNX tunnelling connection, and the lifecycle states of its session.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <cstdint>
    #endif

namespace kmx::aio::knx
{
    /// @brief Timing and retry policy of a tunnelling connection.
    /// @details The four services a client sends are not on the same clock. A tunnelling request is
    ///          acknowledged by the interface within a second; a connect, heartbeat or disconnect involves
    ///          a device that may be busy, and the specification allows ten. One shared value therefore
    ///          cannot be right for all of them: at one second a connect gives up after three attempts in
    ///          about three seconds, which a slow interface loses.
    /// @reference KNX System Specifications, 03/08/02 "Core", timeouts.
    struct tunnelling_config
    {
        /// @brief How many times a request is re-sent before the session gives up.
        /// @note Two retries means three attempts in total.
        std::uint16_t max_retries = 2u;
        /// @brief How long to wait for a TUNNELLING_ACK.
        std::uint32_t ack_timeout_ms = 1'000u;
        /// @brief How long to wait for a CONNECT_RESPONSE.
        std::uint32_t connect_timeout_ms = 10'000u;
        /// @brief How long to wait for a CONNECTIONSTATE_RESPONSE.
        std::uint32_t connectionstate_timeout_ms = 10'000u;
        /// @brief How long to wait for a DISCONNECT_RESPONSE.
        std::uint32_t disconnect_timeout_ms = 10'000u;
        /// @brief How often a supervisor should send a CONNECTIONSTATE_REQUEST.
        /// @note This client runs no timer of its own; the value is what a supervisor driving `heartbeat()`
        ///       should use, and is stated here so it is not reinvented per application.
        std::uint32_t heartbeat_interval_ms = 60'000u;
        /// @brief How many consecutive heartbeat failures close the session.
        /// @note Zero closes it on the first failure rather than disabling the limit.
        std::uint8_t heartbeat_failure_limit = 3u;
        /// @brief How long a connected session may see no traffic at all before it is closed.
        /// @note Checked only when @ref tunnelling_session::check_inactivity is called; nothing here runs
        ///       a timer of its own.
        std::uint32_t inactivity_timeout_ms = 120'000u;
    };

    /// @brief Where a tunnelling session is in its lifecycle.
    /// @details The order is the ordinary path: a session goes idle to connecting to connected, cycles
    ///          through waiting_ack for each request it sends, and ends at closed by way of closing.
    ///          @ref session_state::closed is terminal - only @ref tunnelling_session::reset leaves it.
    enum class session_state : std::uint8_t
    {
        /// @brief No connection and none being attempted.
        idle,
        /// @brief A CONNECT_REQUEST has been sent and its response is outstanding.
        connecting,
        /// @brief The channel is open and idle.
        connected,
        /// @brief A tunnelling request has been sent and its TUNNELLING_ACK is outstanding.
        waiting_ack,
        /// @brief A DISCONNECT_REQUEST has been sent and its response is outstanding.
        closing,
        /// @brief The channel is gone. Terminal until @ref tunnelling_session::reset.
        closed,
    };
}
#endif // KMX_AIO_FEATURE_KNX
