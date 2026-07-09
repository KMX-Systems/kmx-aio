/// @file avb/gptp/clock.hpp
/// @brief Public API for the IEEE 802.1AS gPTP slave clock.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <chrono>
#include <expected>
#include <memory>
#include <string_view>
#include <system_error>

#include <kmx/aio/avb/avb_types.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::avb::gptp
{
    /// @brief IEEE 802.1AS gPTP slave clock.
    ///
    /// Listens for gPTP Announce/Sync/Follow_Up messages from the grandmaster,
    /// measures peer delay via Pdelay_Req/Resp exchange, and runs a PI servo
    /// to discipline the local CLOCK_TAI.
    ///
    /// @note Requires CAP_NET_RAW + CAP_SYS_TIME capabilities.
    /// @note The instance should outlive any coroutines that co_await on it.
    ///
    /// @example
    /// @code
    ///   gptp::clock gm(*exec);
    ///   co_await gm.start("eth0");
    ///   co_await gm.wait_sync(std::chrono::seconds(5));
    ///   auto ts = gm.now();
    /// @endcode
    template <typename Executor>
    class generic_clock
    {
    public:
        /// @brief Creates a gPTP slave clock bound to an executor.
        explicit generic_clock(Executor& exec) noexcept;
        /// @brief Releases clock resources.
        ~generic_clock();

        /// @brief Non-copyable.
        generic_clock(const generic_clock&) = delete;
        /// @brief Non-copyable.
        generic_clock& operator=(const generic_clock&) = delete;
        /// @brief Movable.
        generic_clock(generic_clock&&) = default;
        /// @brief Movable.
        generic_clock& operator=(generic_clock&&) = default;

        /// @brief Bind to a NIC and begin receiving gPTP frames.
        ///        Spawns internal coroutines for Sync, Pdelay, and Announce handling.
        /// @param iface Network interface name.
        /// @return Success or an error code.
        [[nodiscard]] task<std::expected<void, std::error_code>> start(std::string_view iface) noexcept(false);

        /// @brief Return the current TAI time in nanoseconds.
        ///        Reads CLOCK_TAI directly — always available, even before sync.
        /// @return Current TAI time in nanoseconds.
        [[nodiscard]] avb_timestamp_t now() const noexcept;

        /// @brief Suspend until the PI servo reports synchronization, or timeout expires.
        /// @param timeout Maximum time to wait for synchronization.
        /// @return Success once synchronized or an error code on timeout/failure.
        [[nodiscard]] task<std::expected<void, std::error_code>> wait_sync(std::chrono::milliseconds timeout) noexcept(false);

        /// @brief Signed offset from master in nanoseconds (diagnostic).
        /// @return Current offset in nanoseconds.
        [[nodiscard]] std::int64_t offset_ns() const noexcept;

        /// @brief Mean path delay to the grandmaster, in nanoseconds (diagnostic).
        /// @return Current mean path delay in nanoseconds.
        [[nodiscard]] std::int64_t path_delay_ns() const noexcept;

        /// @brief True if the servo has achieved fine-grained synchronization (|offset| < 1µs).
        /// @return `true` if synchronized.
        [[nodiscard]] bool is_synced() const noexcept;

    private:
        /// @brief Opaque implementation state for the clock.
        struct state;
        /// @brief Owned implementation state shared by public operations.
        std::unique_ptr<state> state_;
    };
}
