/// @file api/kmx/aio/knx/routing/client.hpp
/// @brief KNXnet/IP routing client: joins the multicast group and sends and receives on it, with optional IP Secure.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/datagram_transport.hpp>
        #include <kmx/aio/knx/frame.hpp>
        #include <kmx/aio/knx/routing.hpp>
        #include <kmx/aio/knx/routing/sender.hpp>
        #include <kmx/aio/knx/secure/common.hpp>
        #include <kmx/aio/knx/secure/entropy_source.hpp>
        #include <kmx/aio/knx/transport.hpp>
        #include <kmx/aio/task.hpp>

        #include <array>
        #include <atomic>
        #include <cstddef>
        #include <cstdint>
        #include <expected>
        #include <memory>
        #include <optional>
        #include <system_error>
    #endif

namespace kmx::aio::knx::data_secure
{
    class context;
}

namespace kmx::aio::knx::routing
{
    /// @brief A monotonic millisecond clock, for the busy back-off; null selects the steady clock.
    using clock_now_function = std::uint32_t (*)() noexcept;

    /// @brief What a KNX IP Secure routing client joins the backbone with, and what its secure state runs on.
    struct secure_options
    {
        /// @brief The backbone key, latency tolerance and serial number; checked at @ref client::start.
        secure_configuration settings {};
        /// @brief The 64-bit monotonic millisecond clock the routing timer runs on; the steady clock when null.
        secure::monotonic_ms_function clock_ms {};
        /// @brief Where message tags and notify delays come from; @ref kmx::aio::knx::secure::system_entropy when null, and
        ///        otherwise it must outlive the client.
        secure::entropy_source* entropy {};
        /// @brief The 32-bit clock the busy back-off uses; the steady clock when null.
        clock_now_function clock_now {};
    };

    namespace detail
    {
        /// @brief The state a secure routing client holds beyond a plain one; defined in its own detail header.
        struct secure_state;
    }

    /// @brief A KNXnet/IP routing endpoint: joins the multicast group, sends and receives on it.
    /// @details Connectionless, so there is no session to keep - @ref start joins the group and @ref stop
    ///          leaves it, and everything between is individual datagrams. Two behaviours are worth knowing
    ///          about because they are not in the frames themselves: a received ROUTING_BUSY suspends
    ///          sending for the time it asks for, and a datagram identical to the last one sent is treated
    ///          as this client's own traffic reflected back by the group and dropped rather than delivered.
    ///
    ///          Constructed with a @ref secure_configuration, the client speaks KNX IP Secure routing. Every frame
    ///          it sends goes out in a SECURE_WRAPPER under the backbone key, stamped with its routing timer, and
    ///          routing traffic that arrives unwrapped is refused and counted rather than delivered (P1). A wrapper
    ///          is delivered only once its MAC verifies, its timer is inside the latency window, and it is not a
    ///          repeat; nothing about the timer changes before the MAC verifies (P2). Discovery and other services
    ///          routing does not carry are left to the caller exactly as without security. Nothing runs a timer:
    ///          @ref next_timer_deadline_ms says when @ref notify_timer has a TIMER_NOTIFY to send, and a supervisor
    ///          awaits it then. Receives, sends and @ref notify_timer may run concurrently on different threads;
    ///          what they share is guarded by an @ref kmx::aio::async_mutex that is never held across I/O.
    class client final: public sender
    {
    public:
        /// @brief Creates a routing client.
        /// @param transport The executor-bound UDP transport to drive.
        /// @param configuration The multicast group to join; validated at @ref start.
        /// @param clock_now The monotonic millisecond clock; the steady clock when null.
        explicit client(datagram_transport& transport, multicast_configuration configuration = {},
                        clock_now_function clock_now = nullptr) noexcept;

        /// @brief Creates a KNX IP Secure routing client.
        /// @param transport The executor-bound UDP transport to drive.
        /// @param configuration The multicast group to join; use the keyring's multicast address.
        /// @param options The backbone key, latency tolerance and serial number, the clocks, and the entropy source.
        /// @throws std::bad_alloc when the secure state cannot be allocated.
        client(datagram_transport& transport, multicast_configuration configuration, secure_options options) noexcept(false);

        /// @brief Destroys the client, wiping the backbone key it holds.
        ~client() noexcept override;

        /// @brief Validates the configuration and joins the multicast group.
        /// @return Nothing, or the reason the group could not be joined.
        /// @retval kmx::aio::knx::error::invalid_configuration The group is unusable, or a secure client has an
        ///         all-zero serial number (P8).
        /// @retval kmx::aio::knx::error::secure_key_missing A secure client has no backbone key.
        /// @note Idempotent: starting an already-started client succeeds and does nothing. A secure client that
        ///       starts owes a synchronisation request, which @ref notify_timer sends.
        [[nodiscard]] expected_void_t start() noexcept;

        /// @brief Leaves the multicast group and clears the reflection and back-off state.
        /// @return Nothing, or the reason the group could not be left.
        /// @note Idempotent, and the counters survive; only the transient state is cleared.
        [[nodiscard]] expected_void_t stop() noexcept;

        /// @brief Returns what this client has observed since it was created.
        [[nodiscard]] const statistics& counters() const noexcept { return counters_; }

        /// @brief Records a router's back-off request and suspends sending for it.
        /// @param backoff_ms How long to hold off, from now.
        /// @note Called for every ROUTING_BUSY received. It is public so an application that learns of
        ///       congestion by other means can apply the same back-off.
        void note_busy(const std::uint32_t backoff_ms) noexcept;

        /// @brief Records one lost frame.
        void note_lost() noexcept;

        /// @brief Records one datagram of this client's own that came back off the group.
        void note_reflected() noexcept;

        /// @brief Puts one cEMI frame on the multicast group.
        /// @param value The frame to send; its octets must outlive the awaited task.
        /// @return A task yielding nothing, or the error that stopped the send.
        /// @retval kmx::aio::knx::error::invalid_configuration The client has not been started.
        /// @retval kmx::aio::knx::error::timeout A ROUTING_BUSY back-off is still in effect.
        [[nodiscard]] task_returning_expected_void_t send_indication(const indication& value) noexcept(false) override;

        /// @brief Sends a ROUTING_BUSY, asking other senders to hold off.
        /// @param value The device state, wait time and control field to report.
        /// @return A task yielding nothing, or the error that stopped the send.
        [[nodiscard]] task_returning_expected_void_t send_busy(const busy& value) noexcept(false);

        /// @brief Sends a ROUTING_LOST_MESSAGE, reporting frames this device had to drop.
        /// @param value The device state and lost frame count to report.
        /// @return A task yielding nothing, or the error that stopped the send.
        [[nodiscard]] task_returning_expected_void_t send_lost_message(const lost_message& value) noexcept(false);

        /// @brief Waits for the next routing event of any kind.
        /// @return A task yielding the event, or the error that stopped the receive.
        /// @retval kmx::aio::knx::error::invalid_configuration The client has not been started.
        /// @retval kmx::aio::knx::error::connection_failed The datagram came from an address the transport
        ///         could not attribute to a peer.
        /// @retval kmx::aio::knx::error::unsupported_service The datagram is a KNXnet/IP frame of a service
        ///         routing does not define.
        /// @note A datagram identical to one of the recent sends is counted as reflected and waited past, so this
        ///       never yields a client's own traffic back to it.
        [[nodiscard]] event_task_t receive_event() noexcept(false);

        /// @brief Waits for the next ROUTING_INDICATION, handling flow-control events on the way.
        /// @return A task yielding the received frame, or the error that stopped the receive.
        /// @note Busy and lost-message reports are still counted and acted on; they are simply not yielded.
        ///       Use @ref receive_event to see them.
        [[nodiscard]] received_indication_task_t receive_indication() noexcept(false);

        /// @brief Indicates whether this client speaks KNX IP Secure routing.
        [[nodiscard]] bool secured() const noexcept { return secure_ != nullptr; }

        /// @brief Applies KNX Data Secure to the group telegrams this client routes, or stops applying it.
        /// @param context The Data Secure context, which must outlive its use here; null to stop.
        /// @details A frame @ref send_indication puts on the group is secured when it is a group telegram to a group with a
        ///          key. Every indication @ref receive_event and @ref receive_indication yield is opened first, and a
        ///          telegram the context refuses is counted there and read past. A router that only forwards Data Secure
        ///          APDUs, as a gateway does, sets no context.
        void use_data_secure(data_secure::context* const context) noexcept { data_secure_.store(context); }

        /// @brief When @ref notify_timer next has a TIMER_NOTIFY to send, on the secure clock.
        /// @return Zero while a synchronisation request is owed; the maximum value for a client that is not secure.
        /// @note Safe to call from any thread at any time.
        [[nodiscard]] std::uint64_t next_timer_deadline_ms() const noexcept;

        /// @brief Indicates whether the secure routing timer has synchronised, by an answer or by a timeout, so that
        ///        received wrappers are being accepted; false for a client that is not secure.
        /// @note Safe to call from any thread at any time. A telegram sent before this turns true may draw a reply
        ///       that arrives while wrappers are still being dropped.
        [[nodiscard]] bool timer_synchronised() const noexcept;

        /// @brief Sends the TIMER_NOTIFY that is due, if one is; send-only, so it may run while a receive waits.
        /// @return A task yielding nothing, or the error that stopped the send.
        /// @retval kmx::aio::knx::error::invalid_configuration The client is not started or not secure.
        /// @details The first call after @ref start sends the synchronisation request. Later calls send a periodic
        ///          or an update notify when @ref next_timer_deadline_ms has passed, and do nothing otherwise.
        [[nodiscard]] task_returning_expected_void_t notify_timer() noexcept(false);

        /// @brief Returns what the secure layer has refused and done; all zero for a client that is not secure.
        /// @note Read it when no receive, send or notify is in flight.
        [[nodiscard]] const secure::statistics& secure_counters() const noexcept;

    private:
        /// @brief A received datagram's event, or nothing when it was consumed without producing one.
        using optional_event_t = std::optional<event_result_t>;
        /// @brief Whether a TIMER_NOTIFY was prepared for sending, or why none could be.
        using prepared_notify_t = std::expected<bool, std::error_code>;

        /// @brief Reports whether a datagram is this client's own multicast coming back to it.
        [[nodiscard]] bool is_own_reflection(cspan_uint8_t packet) const noexcept;
        void record_sent_packet(cspan_uint8_t packet) noexcept;
        /// @brief Turns one received datagram into the event it represents.
        [[nodiscard]] event_result_t to_event(std::uint16_t service, cspan_uint8_t packet) noexcept;
        /// @brief Turns a ROUTING_INDICATION into an owning event.
        [[nodiscard]] event_result_t to_indication(cspan_uint8_t packet) noexcept;
        /// @brief Opens a received indication with Data Secure when a context is set; false when the context refused it.
        [[nodiscard]] bool open_data_secure(event_result_t& outcome) const noexcept(false);
        [[nodiscard]] expected_socket_address_t multicast_peer() const noexcept;
        [[nodiscard]] static bool valid_source_peer(const transport_peer& peer) noexcept;
        [[nodiscard]] std::uint32_t now_ms() const noexcept;

        /// @brief Refuses a secure configuration that cannot run: no serial number (P8), or no key.
        [[nodiscard]] expected_void_t validate_secure() const noexcept;
        /// @brief Sends one encoded datagram to the group, wrapping it first when the client is secure.
        [[nodiscard]] task_returning_expected_void_t send_packet(cspan_uint8_t packet) noexcept(false);
        /// @brief Wraps an outgoing datagram under the timer, recording it as sent before it leaves.
        [[nodiscard]] task_returning_expected_size_t seal_outgoing(span_uint8_t destination, cspan_uint8_t packet) noexcept(false);
        /// @brief Builds the TIMER_NOTIFY that is due into @p packet; false when none is.
        [[nodiscard]] task<prepared_notify_t> prepare_timer_notify(span_uint8_t packet) noexcept(false);
        /// @brief Handles one datagram without security.
        [[nodiscard]] optional_event_t plain_event(cspan_uint8_t packet) noexcept;
        /// @brief Handles one datagram with security, under the mutex.
        [[nodiscard]] task<optional_event_t> secure_event(cspan_uint8_t packet) noexcept(false);
        /// @brief Dispatches a datagram that is not a reflection; the mutex is held.
        [[nodiscard]] optional_event_t secured_event(std::uint16_t service, cspan_uint8_t packet) noexcept;
        /// @brief Verifies and unwraps a SECURE_WRAPPER; the mutex is held.
        [[nodiscard]] optional_event_t unwrap_event(cspan_uint8_t packet) noexcept;
        /// @brief Verifies and applies a TIMER_NOTIFY; the mutex is held.
        void apply_timer_notify(cspan_uint8_t packet) noexcept;

        datagram_transport& transport_;
        multicast_configuration configuration_ {};
        bool started_ {};
        statistics counters_ {};
        clock_now_function clock_now_ {};
        std::atomic<std::uint32_t> busy_until_ms_ {};
        inline static constexpr std::size_t reflection_history_size = 4u;
        struct sent_packet
        {
            std::array<std::uint8_t, frame::max_datagram_size> bytes {};
            std::size_t size {};
        };
        std::array<sent_packet, reflection_history_size> recent_sent_packets_ {};
        std::size_t recent_sent_packet_count_ {};
        std::size_t next_sent_packet_index_ {};
        std::array<std::uint8_t, frame::max_datagram_size> receive_buffer_ {};
        std::unique_ptr<detail::secure_state> secure_ {};
        /// @brief The Data Secure context applied to group telegrams, when one is set; see @ref use_data_secure.
        std::atomic<data_secure::context*> data_secure_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
