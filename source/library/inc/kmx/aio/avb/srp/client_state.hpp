/// @file avb/srp/client_state.hpp
/// @brief Private implementation state of the IEEE 802.1Qat SRP (MSRP) client.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <chrono>
    #include <cstdint>
    #include <map>
    #include <optional>
    #include <source_location>
    #include <vector>

    #include <kmx/aio/avb/eth_socket.hpp>
    #include <kmx/aio/avb/srp/client.hpp>
    #include <kmx/aio/avb/srp/messages.hpp>
    #include <kmx/logger.hpp>
#endif

namespace kmx::aio::avb::srp
{
    /// @brief Executor-agnostic part of the SRP client: declarations, PDU encoding and decoding.
    /// @details Holds everything that does not depend on the executor type, so the code is emitted once
    ///          instead of once per pillar. `generic_client<Executor>::state` adds the socket and the
    ///          executor-specific coroutine loops.
    struct primary_client
    {
        /// @brief Strict-weak ordering over `stream_id_t`, so it can key the declaration maps.
        struct stream_id_less
        {
            /// @brief Orders by source MAC first, then by unique id.
            /// @param lhs Left operand.
            /// @param rhs Right operand.
            /// @return `true` when @p lhs sorts before @p rhs.
            [[nodiscard]] bool operator()(const stream_id_t& lhs, const stream_id_t& rhs) const noexcept
            {
                if (lhs.source_mac != rhs.source_mac)
                    return lhs.source_mac < rhs.source_mac;
                return lhs.unique_id < rhs.unique_id;
            }
        };

        /// @brief One in-flight `subscribe` call awaiting a matching Talker Advertise.
        struct sub_waiter
        {
            /// @brief The stream being waited on.
            stream_id_t id {};
            /// @brief The descriptor decoded from the advertise, once one arrives.
            std::optional<stream_descriptor> resolved {};
        };

        // Talker: streams we are advertising (stream_id → descriptor)

        /// @brief Streams this node advertises as a talker.
        std::map<stream_id_t, stream_descriptor, stream_id_less> talker_streams_ {};

        // Listener: streams we have subscribed to

        /// @brief Streams this node has subscribed to as a listener.
        std::map<stream_id_t, stream_descriptor, stream_id_less> listener_streams_ {};

        // Pending subscribe waiters: stream_id → resolved descriptor.
        // Stored in a map so coroutine references remain stable across suspension.

        /// @brief Waiters keyed by stream id; map nodes keep references stable across suspension.
        std::map<stream_id_t, sub_waiter, stream_id_less> pending_subs_ {};

        // Encode helpers

        /// @brief Encodes one MSRP Talker Advertise PDU for a stream.
        /// @param desc The stream being advertised.
        /// @return The frame bytes ready to hand to the socket.
        [[nodiscard]] static std::vector<std::byte> build_talker_advertise(const stream_descriptor& desc) noexcept;

        /// @brief Encodes one MSRP Listener Ready PDU for a stream.
        /// @param desc The stream being subscribed to.
        /// @return The frame bytes ready to hand to the socket.
        [[nodiscard]] static std::vector<std::byte> build_listener_ready(const stream_descriptor& desc) noexcept;

        /// @brief Encodes the SR Class A domain declaration PDU.
        /// @return The frame bytes ready to hand to the socket.
        [[nodiscard]] static std::vector<std::byte> build_domain() noexcept;

        // Frame dispatch

        /// @brief Routes one received MSRP frame to the matching handler.
        /// @param data Frame bytes; frames shorter than a message header are dropped.
        /// @param len  Frame length.
        void dispatch(const std::byte* data, std::size_t len) noexcept;

        /// @brief Decodes a Talker Advertise and resolves any `subscribe` waiter for that stream.
        /// @param data Frame bytes.
        /// @param len  Frame length.
        void on_talker_advertise(const std::byte* data, std::size_t len) noexcept;
    };

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
        task_returning_expected_void_t talker_loop() noexcept(false)
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

        /// @brief Detachable wrapper around `talker_loop` that logs a terminal failure.
        task<void> talker_loop_task() noexcept(false)
        {
            const auto res = co_await talker_loop();
            if (!res)
                kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SRP re-declaration loop failed: {}",
                                 res.error().message());
        }
    };
}
