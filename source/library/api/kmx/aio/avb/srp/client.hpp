/// @file avb/srp/client.hpp
/// @brief Public API for the IEEE 802.1Qat SRP (MSRP) stream reservation client.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <chrono>
#include <expected>
#include <memory>
#include <string_view>
#include <system_error>

#include <kmx/aio/avb/avb_types.hpp>
#include <kmx/aio/avb/srp/messages.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::avb::srp
{
    /// @brief IEEE 802.1Qat SRP stream reservation client.
    ///
    /// Implements the MSRP Talker and Listener roles:
    ///  - **Talker**: periodically advertises a stream; switches withdraw on demand.
    ///  - **Listener**: monitors incoming Talker Advertise PDUs for a desired stream
    ///                  and replies with a Listener Ready declaration.
    ///
    /// @note Requires CAP_NET_RAW and an AVB-capable switch for end-to-end reservation.
    ///       On a single-host loopback configuration the SRP PDUs are still exchanged
    ///       between end-stations even without a managed switch.
    ///
    /// @example
    /// @code
    ///   srp::client srp(*exec);
    ///   co_await srp.start("eth0");
    ///
    ///   // Talker role: reserve bandwidth for this stream
    ///   srp::stream_descriptor desc { ... };
    ///   co_await srp.advertise(desc);
    ///
    ///   // Listener role: wait for a talker and subscribe
    ///   co_await srp.subscribe(desc.stream_id);
    /// @endcode
    template <typename Executor>
    class generic_client
    {
    public:
        explicit generic_client(Executor& exec) noexcept;
        ~generic_client();

        generic_client(const generic_client&)            = delete;
        generic_client& operator=(const generic_client&) = delete;
        generic_client(generic_client&&)                 = default;
        generic_client& operator=(generic_client&&)      = default;

        /// @brief Bind to a NIC and start receiving MSRP frames.
        ///        Spawns the receive loop and domain advertisement.
        [[nodiscard]] task<std::expected<void, std::error_code>>
        start(std::string_view iface) noexcept(false);

        /// @brief **Talker**: advertise a stream and periodically re-declare it.
        ///        Returns once the first declaration is sent.
        [[nodiscard]] task<std::expected<void, std::error_code>>
        advertise(const stream_descriptor& desc) noexcept(false);

        /// @brief **Listener**: wait until a Talker Advertise for the given stream_id
        ///        is received, then send a Listener Ready declaration.
        ///        Suspends until the talker is found or timeout expires.
        [[nodiscard]] task<std::expected<stream_descriptor, std::error_code>>
        subscribe(const stream_id_t& stream_id,
                  std::chrono::milliseconds timeout = std::chrono::seconds(5)) noexcept(false);

        /// @brief **Talker / Listener**: withdraw a previously advertised or subscribed stream.
        [[nodiscard]] task<std::expected<void, std::error_code>>
        withdraw(const stream_id_t& stream_id) noexcept(false);

    private:
        struct state;
        std::unique_ptr<state> state_;
    };
}

// Pillar aliases
#include <kmx/aio/completion/executor.hpp>

namespace kmx::aio::completion::avb::srp
{
    using client = kmx::aio::avb::srp::generic_client<kmx::aio::completion::executor>;
}
