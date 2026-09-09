/// @file aio/knx/client.hpp
/// @brief Coroutine-oriented KNXnet/IP tunnelling client boundary.
/// @details
/// The application-facing end of tunnelling. @ref kmx::aio::knx::tunnelling_client owns a transport, a
/// clock and a @ref kmx::aio::knx::tunnelling_session, and turns the session's prepare-send-feed-back
/// protocol into ordinary awaitable calls: `connect`, `send`, `heartbeat`, `disconnect`, and the group
/// value services that most applications actually want.
///
/// Retries and timeouts happen inside these calls rather than around them - awaiting @ref
/// kmx::aio::knx::tunnelling_client::send returns once the request has been acknowledged or the retry
/// budget is spent, so a caller writes no retry loop of its own.
///
/// Only one operation may be in flight at a time, and a second concurrent call is refused with
/// @ref kmx::aio::knx::error::send_queue_full rather than being queued. That is the protocol's own
/// constraint showing through: tunnelling allows a single unacknowledged request per channel, so there is
/// nothing a queue could usefully do with the second one.
/// @reference KNX System Specifications, 03/08/04 "Tunnelling".
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <atomic>
        #include <array>
        #include <cstdint>
        #include <expected>
        #include <span>
        #include <sys/socket.h>
        #include <vector>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/task.hpp>
    #include <kmx/aio/knx/cemi.hpp>
    #include <kmx/aio/knx/connection.hpp>
    #include <kmx/aio/knx/dpt.hpp>
    #include <kmx/aio/knx/session.hpp>
    #include <kmx/aio/knx/secure.hpp>
    #include <kmx/aio/knx/transport.hpp>

namespace kmx::aio::knx
{
    /// @brief A monotonic millisecond clock; null selects the steady clock.
    using clock_now_function = std::uint32_t (*)() noexcept;

    /// @brief A received cEMI message together with the octets it was decoded from.
    /// @details The decoded frame names its payload by offset, so the octets have to travel with it. Owning
    ///          them here is what lets a telegram outlive the receive buffer it arrived in.
    struct telegram
    {
        /// @brief The decoded message.
        cemi_frame frame {};
        /// @brief The cEMI octets the message was decoded from.
        byte_buffer_t bytes {};

        /// @brief Returns the application payload.
        [[nodiscard]] cspan_uint8_t payload() const noexcept { return frame.payload(bytes); }
        /// @brief Returns the value view a datapoint decoder accepts.
        [[nodiscard]] dpt::value_view value() const noexcept { return dpt::make_value_view(frame, bytes); }

        /// @brief Decodes the application value as the named datapoint main type.
        /// @tparam Main The datapoint main type.
        /// @return The decoded value, or the reason it could not be decoded.
        template <std::uint16_t Main>
        [[nodiscard]] dpt::decode_result_t<Main> value_as() const noexcept
        {
            return dpt::traits<Main>::decode(value());
        }
    };

    /// @brief A received telegram, or the error explaining why none was obtained.
    using telegram_result_t = std::expected<telegram, std::error_code>;
    /// @brief Task yielding a received telegram or the error that stopped the receive.
    using telegram_task_t = task<telegram_result_t>;
    /// @brief Task yielding raw cEMI octets or the error that stopped the receive.
    using cemi_bytes_task_t = task<expected_byte_buffer_t>;

    /// @brief A KNXnet/IP tunnelling client: one connection to one interface.
    /// @note Not thread-safe, and deliberately not queued. One operation may be outstanding; a concurrent
    ///       second reports @ref kmx::aio::knx::error::send_queue_full.
    class tunnelling_client final
    {
    public:
        /// @brief Creates a client bound to one KNXnet/IP control endpoint.
        /// @param transport The executor-bound UDP transport to drive.
        /// @param peer The interface's control endpoint; only @p peer_length octets are read.
        /// @param peer_length The number of octets @p peer provides - `sizeof(sockaddr_in)` for IPv4.
        /// @param config The tunnelling timing and retry policy.
        /// @param clock_now The monotonic millisecond clock; the steady clock when null.
        /// @param secure_config The KNX Secure profile and replay policy.
        /// @param secure_provider The cryptographic provider; required when a profile is selected.
        /// @note This is the overload to use when holding a `sockaddr_in` or `sockaddr_in6`. Casting one
        ///       to `sockaddr_storage&` to reach the overload below reads past the object.
        tunnelling_client(datagram_transport& transport,
                          const sockaddr* peer,
                          ::socklen_t peer_length,
                          tunnelling_config config = {},
                          clock_now_function clock_now = nullptr,
                          secure::configuration secure_config = {},
                          secure::provider* secure_provider = nullptr) noexcept;

        /// @brief Creates a client from a peer the caller already holds as `sockaddr_storage`.
        /// @copydetails tunnelling_client
        tunnelling_client(datagram_transport& transport,
                          const sockaddr_storage& peer,
                          const ::socklen_t peer_length,
                          const tunnelling_config config = {},
                          const clock_now_function clock_now = nullptr,
                          const secure::configuration secure_config = {},
                          secure::provider* const secure_provider = nullptr) noexcept:
            tunnelling_client(transport, reinterpret_cast<const sockaddr*>(&peer), peer_length, config, clock_now, secure_config,
                              secure_provider)
        {
        }
        tunnelling_client(const tunnelling_client&) = delete;
        tunnelling_client& operator=(const tunnelling_client&) = delete;
        /// @brief Destroys the client; an open channel is abandoned, not disconnected.
        /// @note Await @ref disconnect first to close the channel politely; otherwise the interface holds
        ///       it until its own inactivity timeout.
        ~tunnelling_client() noexcept = default;

        /// @brief Returns where the session is in its lifecycle.
        [[nodiscard]] session_state state() const noexcept { return session_.state(); }

        /// @brief Indicates whether the channel is open and idle.
        [[nodiscard]] bool connected() const noexcept { return state() == session_state::connected; }

        /// @brief Indicates whether a disconnect is in progress.
        [[nodiscard]] bool closing() const noexcept { return state() == session_state::closing; }

        /// @brief Indicates whether the channel is gone; only @ref reset leaves this state.
        [[nodiscard]] bool closed() const noexcept { return state() == session_state::closed; }

        /// @brief Returns when traffic was last seen, on this client's clock.
        [[nodiscard]] std::uint32_t last_activity_ms() const noexcept { return session_.last_activity_ms(); }

        /// @brief Closes the session if it has been silent past its inactivity timeout.
        /// @return Nothing while the session is alive.
        /// @retval kmx::aio::knx::error::inactivity_timeout The session was silent too long and is now
        ///         closed.
        /// @note Nothing here runs a timer; a supervisor that is not otherwise awaiting the client calls
        ///       this to have the timeout noticed.
        [[nodiscard]] expected_void_t poll() noexcept
        {
            return session_.check_inactivity(now_ms());
        }

        /// @brief Returns the channel the interface allocated; zero before a connection is established.
        [[nodiscard]] std::uint8_t channel_id() const noexcept
        {
            return static_cast<std::uint8_t>(session_.channel_id());
        }
        /// @brief Returns the individual address the interface assigned to this tunnel.
        /// @note Unset until a CONNECT_RESPONSE has been accepted.
        [[nodiscard]] individual_address assigned_address() const noexcept { return session_.assigned_address(); }

        /// @brief Opens a tunnelling connection, retrying until the budget is spent.
        /// @param request The connection to ask for.
        /// @return A task yielding nothing, or the reason no connection was established.
        /// @retval kmx::aio::knx::error::send_queue_full Another operation is already in flight.
        /// @retval kmx::aio::knx::error::invalid_configuration The secure configuration is inconsistent,
        ///         or the session is not idle.
        /// @retval kmx::aio::knx::error::secure_unsupported A secure profile is selected but no provider
        ///         was supplied.
        /// @retval kmx::aio::knx::error::connection_failed The interface refused the connection.
        /// @retval kmx::aio::knx::error::timeout No response arrived within the retry budget.
        /// @note An all-zero HPAI in @p request is honoured as the route-back form, not rejected: it asks
        ///       the interface to answer the source of the datagram, which is how a client behind NAT is
        ///       reachable at all.
        [[nodiscard]] task_returning_expected_void_t connect(
            const connect_request_frame& request) noexcept(false);

        /// @brief Opens a tunnelling connection over IPv6.
        /// @param request The connection to ask for.
        /// @return A task yielding nothing, or the reason no connection was established.
        [[nodiscard]] task_returning_expected_void_t connect(
            const ipv6_connect_request_frame& request) noexcept(false);

        /// @brief Tunnels one cEMI frame and waits for its acknowledgement.
        /// @param cemi_bytes The frame to send.
        /// @return A task yielding nothing, or the reason the frame was not delivered.
        /// @retval kmx::aio::knx::error::send_queue_full Another operation is already in flight.
        /// @retval kmx::aio::knx::error::payload_too_large @p cemi_bytes exceeds what a tunnelling request
        ///         can carry.
        /// @retval kmx::aio::knx::error::shutdown The session is closing or closed.
        /// @retval kmx::aio::knx::error::timeout No acknowledgement arrived within the retry budget; the
        ///         session is then closed.
        /// @note Returning means the interface acknowledged the frame, not that a device acted on it.
        [[nodiscard]] task_returning_expected_void_t send(
            cspan_uint8_t cemi_bytes) noexcept(false);

        /// @brief Applies the configured secure profile to a payload.
        /// @param payload The payload to protect.
        /// @param sequence The sequence number to protect it under.
        /// @return The protected octets, or the reason they could not be produced.
        /// @retval kmx::aio::knx::error::secure_unsupported A profile is selected but no provider was
        ///         supplied.
        /// @note With no profile selected the payload is returned copied and unchanged, so a caller need
        ///       not branch on whether security is configured.
        [[nodiscard]] expected_byte_buffer_t protect_payload(cspan_uint8_t payload, std::uint64_t sequence) const noexcept;

        /// @brief Recovers a payload protected under the configured secure profile.
        /// @param payload The protected octets.
        /// @param sequence The sequence number they were protected under.
        /// @return The recovered payload, or the reason verification failed.
        /// @note With no profile selected the payload is returned copied and unchanged.
        [[nodiscard]] expected_byte_buffer_t unprotect_payload(cspan_uint8_t payload, std::uint64_t sequence) const noexcept;

        /// @brief Sends a CONNECTIONSTATE_REQUEST and waits for its answer.
        /// @return A task yielding nothing, or the reason the channel was not confirmed.
        /// @retval kmx::aio::knx::error::send_queue_full Another operation is already in flight.
        /// @retval kmx::aio::knx::error::shutdown The session is not connected.
        /// @retval kmx::aio::knx::error::connection_failed The heartbeat failed, but under the limit.
        /// @retval kmx::aio::knx::error::heartbeat_failed The failure limit was reached; the session is
        ///         now closed.
        /// @note Nothing here schedules this; a supervisor sends it every
        ///       @ref tunnelling_config::heartbeat_interval_ms while the connection is otherwise idle.
        [[nodiscard]] task_returning_expected_void_t heartbeat() noexcept(false);

        /// @brief Waits for the next datagram on this connection and applies it to the session.
        /// @return A task yielding the decoded datagram, or the error that stopped the receive.
        /// @retval kmx::aio::knx::error::send_queue_full Another operation is already in flight.
        /// @retval kmx::aio::knx::error::connection_failed The datagram came from an address that is not
        ///         this connection's peer.
        /// @note The lowest-level receive; use it to observe connection management traffic the higher-level
        ///       receives handle and discard.
        [[nodiscard]] datagram_task_t receive_datagram() noexcept(false);

        /// @brief Waits for the next tunnelled cEMI frame, as raw octets.
        /// @return A task yielding the cEMI octets, or the error that stopped the receive.
        /// @retval kmx::aio::knx::error::send_queue_full Another operation is already in flight.
        /// @note Acknowledges the request and skips duplicates, so what comes back is a frame not yet seen.
        [[nodiscard]] cemi_bytes_task_t receive_cemi() noexcept(false);

        /// @brief Waits for the next tunnelled cEMI frame, decoded.
        /// @return A task yielding the telegram, or the error that stopped the receive.
        /// @note The usual receive: @ref telegram owns its octets, so it outlives the receive buffer and
        ///       can be decoded with @ref telegram::value_as at leisure.
        [[nodiscard]] telegram_task_t receive_telegram() noexcept(false);

        /// @brief Closes the channel and waits for the interface to confirm.
        /// @return A task yielding nothing, or the reason the close did not complete.
        /// @retval kmx::aio::knx::error::send_queue_full Another operation is already in flight.
        /// @retval kmx::aio::knx::error::invalid_configuration The session is not connected.
        /// @retval kmx::aio::knx::error::timeout The interface never confirmed; the session is closed
        ///         locally regardless.
        [[nodiscard]] task_returning_expected_void_t disconnect() noexcept(false);

        /// @brief Sends an A_GroupValue_Write telegram.
        /// @param destination The group to write to.
        /// @param value The value to write, as encoded by the datapoint layer.
        /// @param options The link layer flags.
        /// @return Nothing, or the reason the telegram could not be sent.
        /// @note The source address is left unset so the interface substitutes the address it assigned.
        [[nodiscard]] task_returning_expected_void_t write_group_value(group_address destination, const dpt::payload& value,
                                                                      l_data_options options = {}) noexcept(false);

        /// @brief Sends an A_GroupValue_Read telegram.
        /// @param destination The group to read from.
        /// @param options The link layer flags.
        /// @return Nothing, or the reason the telegram could not be sent.
        [[nodiscard]] task_returning_expected_void_t read_group_value(group_address destination,
                                                                     l_data_options options = {}) noexcept(false);

        /// @brief Sends an A_GroupValue_Response telegram.
        /// @param destination The group the response belongs to.
        /// @param value The value to report, as encoded by the datapoint layer.
        /// @param options The link layer flags.
        /// @return Nothing, or the reason the telegram could not be sent.
        [[nodiscard]] task_returning_expected_void_t respond_group_value(group_address destination, const dpt::payload& value,
                                                                        l_data_options options = {}) noexcept(false);

        /// @brief Abandons the connection locally, without sending anything.
        /// @note For a transport that has already failed, where a DISCONNECT_REQUEST cannot be delivered.
        ///       Leaves the session closed; @ref reset is what makes the client usable again.
        void shutdown() noexcept;

        /// @brief Returns the client to its initial state, ready to connect again.
        /// @note Clears the session, the data endpoint learnt from the last CONNECT_RESPONSE, and the
        ///       secure sequence and replay window. The configuration and peer are kept.
        void reset() noexcept;

    private:
        class operation_guard
        {
        public:
            explicit operation_guard(tunnelling_client& owner) noexcept;
            ~operation_guard() noexcept;
            operation_guard(const operation_guard&) = delete;
            operation_guard& operator=(const operation_guard&) = delete;

            [[nodiscard]] bool acquired() const noexcept { return acquired_; }

        private:
            tunnelling_client* owner_ {};
            bool acquired_ {};
        };

        struct received_cemi
        {
            cemi_frame frame {};
            byte_buffer_t bytes {};
        };

        /// @brief A received cEMI message, or the error explaining why none was obtained.
        using received_cemi_result_t = std::expected<received_cemi, std::error_code>;
        /// @brief Task yielding a received cEMI message or the error that stopped the receive.
        using received_cemi_task_t = task<received_cemi_result_t>;

        enum class endpoint_kind : std::uint8_t
        {
            control,
            data,
        };

        [[nodiscard]] bool peer_matches(const transport_peer& peer, endpoint_kind kind) const noexcept;
        void clear_data_peer() noexcept;
        [[nodiscard]] std::uint32_t now_ms() const noexcept;
        [[nodiscard]] std::uint32_t operation_deadline_ms() const noexcept;
        [[nodiscard]] std::uint32_t connect_deadline_ms() const noexcept;
        [[nodiscard]] std::uint32_t disconnect_deadline_ms() const noexcept;
        [[nodiscard]] std::uint32_t connectionstate_deadline_ms() const noexcept;
        [[nodiscard]] task_returning_expected_void_t send_packet(
            cspan_uint8_t packet, endpoint_kind kind) noexcept(false);
        [[nodiscard]] datagram_result_t decode_received_packet(cspan_uint8_t packet, endpoint_kind kind) noexcept;
        [[nodiscard]] expected_byte_buffer_t secure_wrap_data_packet(cspan_uint8_t packet, std::uint64_t sequence) const noexcept;
        [[nodiscard]] bool secure_data_enabled() const noexcept
        {
            return secure_config_.selected != secure::profile::none;
        }
        [[nodiscard]] std::uint64_t next_secure_sequence() noexcept { return secure_sequence_++; }
        void reset_secure_state() noexcept
        {
            secure_sequence_ = 1u;
            secure_replay_ = secure::replay_window_state {secure_config_.replay_window};
        }
        [[nodiscard]] datagram_task_t receive_datagram_impl() noexcept(false);
        [[nodiscard]] received_cemi_task_t receive_cemi_impl() noexcept(false);
        /// @brief Indicates whether a refused frame is the previous one being retried by the server.
        [[nodiscard]] bool is_retransmission(const expected_void_t& accepted,
                                             const tunnelling_request_frame& request) const noexcept;
        /// @brief Checks the session is open and has not gone silent past its inactivity timeout.
        [[nodiscard]] expected_void_t check_session_live() noexcept;
        /// @brief Handles a datagram that is session bookkeeping rather than a tunnelled frame.
        [[nodiscard]] expected_void_t absorb_session_datagram(const datagram& value) noexcept;
        /// @brief Chooses which endpoint a service's datagram is expected to have arrived from.
        [[nodiscard]] endpoint_kind endpoint_for(std::uint16_t service_type) const noexcept;
        /// @brief Sends one datagram on the data channel, wrapping it when that channel is secured.
        [[nodiscard]] task_returning_expected_void_t send_data_packet(cspan_uint8_t packet) noexcept(false);
        /// @brief Acknowledges one tunnelled frame.
        [[nodiscard]] task_returning_expected_void_t acknowledge(const tunnelling_request_frame& request) noexcept(false);
        /// @brief Resends one TUNNELLING_REQUEST until it is acknowledged or the session stops retrying.
        [[nodiscard]] task_returning_expected_void_t retry_request(cspan_uint8_t packet) noexcept(false);
        /// @brief Resends one CONNECTIONSTATE_REQUEST until it is answered or the connection is lost.
        [[nodiscard]] task_returning_expected_void_t retry_heartbeat(cspan_uint8_t packet) noexcept(false);
        /// @brief Resends one DISCONNECT_REQUEST until the peer answers it or the session stops retrying.
        [[nodiscard]] task_returning_expected_void_t await_disconnect_response(cspan_uint8_t packet) noexcept(false);
        /// @brief Reads a datagram received while closing; nothing when it was not the answer awaited.
        [[nodiscard]] optional_expected_void_t on_disconnect_datagram(const transport_peer& peer, std::size_t size) noexcept;
        /// @brief Checks the secure configuration is usable, and that a provider is present when needed.
        [[nodiscard]] expected_void_t validate_secure_ready() const noexcept;
        /// @brief Resends one CONNECT_REQUEST until the session answers, fails, or stops retrying.
        [[nodiscard]] task_returning_expected_void_t retry_connect(cspan_uint8_t packet) noexcept(false);
        /// @brief Records the IPv4 data endpoint a CONNECT_RESPONSE named.
        void adopt_data_peer(const hpai& endpoint) noexcept;
        /// @brief Records the IPv6 data endpoint a CONNECT_RESPONSE named.
        void adopt_data_peer(const ipv6_hpai& endpoint) noexcept;
        /// @brief Takes the data endpoint from a CONNECT_RESPONSE, if the datagram is one.
        [[nodiscard]] expected_void_t adopt_data_endpoint(const datagram& value) noexcept;
        [[nodiscard]] task_returning_expected_void_t receive_into_session(endpoint_kind kind, std::uint32_t deadline_ms) noexcept(false);
        [[nodiscard]] task_returning_expected_void_t send_group_service(group_address destination, apci service, const apdu_payload& value,
                                                                       const l_data_options& options) noexcept(false);

        datagram_transport& transport_;
        sockaddr_storage peer_ {};
        ::socklen_t peer_length_ = 0u;
        bool configured_peer_valid_ {};
        sockaddr_storage data_peer_ {};
        ::socklen_t data_peer_length_ = 0u;
        bool data_peer_valid_ {};
        clock_now_function clock_now_ {};
        secure::configuration secure_config_ {};
        secure::provider* secure_provider_ {};
        std::uint64_t secure_sequence_ = 1u;
        secure::replay_window_state secure_replay_ {};
        std::atomic_bool operation_active_ {};
        tunnelling_session session_;
        std::array<std::uint8_t, frame::max_datagram_size> receive_buffer_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
