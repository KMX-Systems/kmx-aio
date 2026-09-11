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
///
/// A stream transport - KNXnet/IP over TCP - changes what follows from a connection that already delivers every frame
/// once and in order. `connect` opens the connection and asks for the tunnel with TCP HPAIs. Nothing is acknowledged,
/// so `send` and `heartbeat` only send: they take turns rather than being refused, and either may run while a task
/// waits in a receive, which absorbs the heartbeat's answer. An answer that never comes is reported by `poll`.
/// `disconnect`, `shutdown` and `reset` close the connection, so a reconnect runs over a new one.
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
        #include <memory>
        #include <mutex>
        #include <optional>
        #include <span>
        #include <sys/socket.h>
        #include <type_traits>
        #include <utility>
        #include <vector>
    #endif

    #include <kmx/aio/async_mutex.hpp>
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/task.hpp>
    #include <kmx/aio/knx/cemi.hpp>
    #include <kmx/aio/knx/connection.hpp>
    #include <kmx/aio/knx/dpt.hpp>
    #include <kmx/aio/knx/secure/common.hpp>
    #include <kmx/aio/knx/secure/credentials.hpp>
    #include <kmx/aio/knx/secure/entropy.hpp>
    #include <kmx/aio/knx/session.hpp>
    #include <kmx/aio/knx/transport.hpp>

namespace kmx::aio::knx::secure
{
    class tunnel_transport;
}

namespace kmx::aio::knx::data_secure
{
    class context;
}

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
    /// @note Deliberately not queued. One operation may be outstanding; a concurrent second reports
    ///       @ref kmx::aio::knx::error::send_queue_full. On a stream transport `send` and `heartbeat` are the
    ///       exception: they take turns with each other and may overlap a receive resumed on another thread.
    class tunnelling_client final
    {
    public:
        /// @brief Creates a client bound to one KNXnet/IP control endpoint.
        /// @param transport The executor-bound UDP transport to drive.
        /// @param peer The interface's control endpoint; only @p peer_length octets are read.
        /// @param peer_length The number of octets @p peer provides - `sizeof(sockaddr_in)` for IPv4.
        /// @param config The tunnelling timing and retry policy.
        /// @param clock_now The monotonic millisecond clock; the steady clock when null.
        /// @note This is the overload to use when holding a `sockaddr_in` or `sockaddr_in6`. Casting one
        ///       to `sockaddr_storage&` to reach the overload below reads past the object.
        tunnelling_client(datagram_transport& transport,
                          const sockaddr* peer,
                          ::socklen_t peer_length,
                          tunnelling_config config = {},
                          clock_now_function clock_now = nullptr) noexcept;

        /// @brief Creates a client from a peer the caller already holds as `sockaddr_storage`.
        /// @copydetails tunnelling_client
        tunnelling_client(datagram_transport& transport,
                          const sockaddr_storage& peer,
                          const ::socklen_t peer_length,
                          const tunnelling_config config = {},
                          const clock_now_function clock_now = nullptr) noexcept:
            tunnelling_client(transport, reinterpret_cast<const sockaddr*>(&peer), peer_length, config, clock_now)
        {
        }
        /// @brief Creates a client that tunnels through a KNX IP Secure session.
        /// @param transport The stream transport - KNXnet/IP over TCP - the session runs over; a datagram transport is
        ///        refused when connecting.
        /// @param peer The interface's control endpoint; only @p peer_length octets are read.
        /// @param peer_length The number of octets @p peer provides.
        /// @param config The tunnelling timing and retry policy.
        /// @param credentials The user id, keys and serial number the session is opened with.
        /// @param clock_now The monotonic millisecond clock of the tunnel; the steady clock when null.
        /// @param clock_ms The monotonic clock the session's keep-alive and timeout run on; the steady clock when null.
        /// @param entropy Where session key pairs come from; the system source when null. Must outlive the client.
        /// @throws std::bad_alloc when the session cannot be allocated.
        /// @details Every frame after the handshake is wrapped, and an unwrapped frame other than discovery is refused
        ///          with @ref kmx::aio::knx::error::secure_frame_required. `connect` runs the handshake before anything
        ///          else is sent, and a handshake that fails, is refused or times out closes the connection again: no
        ///          unencrypted CONNECT_REQUEST is ever sent (P1). `disconnect` ends with a SESSION_STATUS close, and a
        ///          reconnect opens a new connection with a fresh key pair and session (P3).
        tunnelling_client(datagram_transport& transport, const sockaddr* peer, ::socklen_t peer_length, tunnelling_config config,
                          secure::tunnelling_credentials credentials, clock_now_function clock_now = nullptr,
                          secure::monotonic_ms_function clock_ms = nullptr, secure::entropy_source* entropy = nullptr) noexcept(false);

        tunnelling_client(const tunnelling_client&) = delete;
        tunnelling_client& operator=(const tunnelling_client&) = delete;
        /// @brief Destroys the client; an open channel is abandoned, not disconnected.
        /// @note Await @ref disconnect first to close the channel politely; otherwise the interface holds
        ///       it until its own inactivity timeout.
        ~tunnelling_client() noexcept;

        /// @brief Returns where the session is in its lifecycle.
        [[nodiscard]] session_state state() const noexcept
        {
            return with_session([](const tunnelling_session& session) noexcept { return session.state(); });
        }

        /// @brief Indicates whether the channel is open and idle.
        [[nodiscard]] bool connected() const noexcept { return state() == session_state::connected; }

        /// @brief Indicates whether a disconnect is in progress.
        [[nodiscard]] bool closing() const noexcept { return state() == session_state::closing; }

        /// @brief Indicates whether the channel is gone; only @ref reset leaves this state.
        [[nodiscard]] bool closed() const noexcept { return state() == session_state::closed; }

        /// @brief Returns when traffic was last seen, on this client's clock.
        [[nodiscard]] std::uint32_t last_activity_ms() const noexcept
        {
            return with_session([](const tunnelling_session& session) noexcept { return session.last_activity_ms(); });
        }

        /// @brief Closes the session if it has been silent past its inactivity timeout.
        /// @return Nothing while the session is alive.
        /// @retval kmx::aio::knx::error::inactivity_timeout The session was silent too long and is now
        ///         closed.
        /// @retval kmx::aio::knx::error::connection_failed A heartbeat sent over a stream went unanswered, under the
        ///         failure limit.
        /// @retval kmx::aio::knx::error::heartbeat_failed Unanswered heartbeats reached the failure limit; the session
        ///         is now closed, and so is the connection.
        /// @note Nothing here runs a timer; a supervisor that is not otherwise awaiting the client calls
        ///       this to have the timeout noticed.
        [[nodiscard]] expected_void_t poll() noexcept;

        /// @brief Returns the channel the interface allocated; zero before a connection is established.
        [[nodiscard]] std::uint8_t channel_id() const noexcept
        {
            return with_session([](const tunnelling_session& session) noexcept { return static_cast<std::uint8_t>(session.channel_id()); });
        }
        /// @brief Returns the individual address the interface assigned to this tunnel.
        /// @note Unset until a CONNECT_RESPONSE has been accepted.
        [[nodiscard]] individual_address assigned_address() const noexcept
        {
            return with_session([](const tunnelling_session& session) noexcept { return session.assigned_address(); });
        }

        /// @brief Indicates whether this client tunnels through a KNX IP Secure session.
        [[nodiscard]] bool is_secure() const noexcept { return secure_ != nullptr; }

        /// @brief Indicates whether @ref keep_alive should be awaited: 50 s have passed since the session last sent.
        /// @note Always false without a secure session.
        [[nodiscard]] bool keep_alive_due() const noexcept;

        /// @brief Sends a SESSION_STATUS keep-alive.
        /// @return A task yielding nothing, or why it was not sent.
        /// @retval kmx::aio::knx::error::invalid_configuration The client has no secure session, or it is not established.
        /// @note Send-only, so it may run while a task waits in a receive. Nothing here schedules it: a supervisor awaits
        ///       it whenever @ref keep_alive_due says so, which keeps a session on a quiet bus from expiring.
        [[nodiscard]] task_returning_expected_void_t keep_alive() noexcept(false);

        /// @brief Returns what the secure session has refused and done; all zero without one.
        /// @note A copy, since the counters change under a lock as frames arrive.
        [[nodiscard]] secure::statistics secure_counters() const noexcept;

        /// @brief Opens a tunnelling connection, retrying until the budget is spent.
        /// @param request The connection to ask for.
        /// @return A task yielding nothing, or the reason no connection was established.
        /// @retval kmx::aio::knx::error::send_queue_full Another operation is already in flight.
        /// @retval kmx::aio::knx::error::invalid_configuration An endpoint is unusable, or the session is
        ///         not idle.
        /// @retval kmx::aio::knx::error::connection_failed The interface refused the connection.
        /// @retval kmx::aio::knx::error::timeout No response arrived within the retry budget.
        /// @note An all-zero HPAI in @p request is honoured as the route-back form, not rejected: it asks
        ///       the interface to answer the source of the datagram, which is how a client behind NAT is
        ///       reachable at all.
        /// @note On a stream transport the connection is opened first, both HPAIs are sent as the TCP HPAI whatever
        ///       @p request names, and one attempt is made; an attempt that fails closes the connection again.
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
        /// @note Returning means the interface acknowledged the frame, not that a device acted on it. On a stream
        ///       transport nothing acknowledges it: returning means it was sent, and a concurrent call waits its turn
        ///       instead of being refused.
        [[nodiscard]] task_returning_expected_void_t send(
            cspan_uint8_t cemi_bytes) noexcept(false);

        /// @brief Sends a CONNECTIONSTATE_REQUEST and waits for its answer.
        /// @return A task yielding nothing, or the reason the channel was not confirmed.
        /// @retval kmx::aio::knx::error::send_queue_full Another operation is already in flight.
        /// @retval kmx::aio::knx::error::shutdown The session is not connected.
        /// @retval kmx::aio::knx::error::connection_failed The heartbeat failed, but under the limit.
        /// @retval kmx::aio::knx::error::heartbeat_failed The failure limit was reached; the session is
        ///         now closed.
        /// @note Nothing here schedules this; a supervisor sends it every
        ///       @ref tunnelling_config::heartbeat_interval_ms while the connection is otherwise idle.
        /// @note On a stream transport this only sends, and may run while a task waits in a receive: that receive
        ///       applies the answer, and @ref poll reports one that has not come within
        ///       @ref tunnelling_config::connectionstate_timeout_ms.
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
        /// @note On a stream transport the connection is closed afterwards, whatever the outcome.
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
        /// @note Clears the session and the data endpoint learnt from the last CONNECT_RESPONSE. The
        ///       configuration and peer are kept.
        void reset() noexcept;

        /// @brief Applies KNX Data Secure to this client's group telegrams, or stops applying it.
        /// @param context The Data Secure context, which must outlive its use here; null to stop.
        /// @details A telegram sent through @ref write_group_value, @ref read_group_value or @ref respond_group_value to a
        ///          group with a key is secured, under the tunnel's assigned address as its source. Every frame
        ///          @ref receive_cemi and @ref receive_telegram return is opened first, and a telegram the context refuses
        ///          is counted there and read past. Frames given to @ref send go out as they are.
        void use_data_secure(data_secure::context* const context) noexcept { data_secure_.store(context); }

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

        /// @brief The outcome of applying a received TUNNELLING_REQUEST to the session.
        struct indication_outcome
        {
            /// @brief Nothing, or why the session refused the request.
            expected_void_t accepted {};
            /// @brief Whether the request repeats the one accepted last.
            bool duplicate {};
        };

        /// @brief Runs one synchronous step on the session, holding the session lock.
        /// @details On a stream transport a receive, the senders and @ref poll reach the session from different tasks,
        ///          which may resume on different threads. The lock is held for the step alone, never across a
        ///          suspension, and @p step must not call a member that takes it again.
        template <typename Step>
        std::invoke_result_t<Step, tunnelling_session&> with_session(Step&& step) noexcept
        {
            const std::lock_guard lock {session_mutex_};
            return std::forward<Step>(step)(session_);
        }

        /// @copydoc with_session
        template <typename Step>
        std::invoke_result_t<Step, const tunnelling_session&> with_session(Step&& step) const noexcept
        {
            const std::lock_guard lock {session_mutex_};
            return std::forward<Step>(step)(std::as_const(session_));
        }

        [[nodiscard]] bool peer_matches(const transport_peer& peer, endpoint_kind kind) const noexcept;
        void clear_data_peer() noexcept;
        [[nodiscard]] std::uint32_t now_ms() const noexcept;
        [[nodiscard]] std::uint32_t operation_deadline_ms() const noexcept;
        [[nodiscard]] std::uint32_t connect_deadline_ms() const noexcept;
        [[nodiscard]] std::uint32_t disconnect_deadline_ms() const noexcept;
        [[nodiscard]] std::uint32_t connectionstate_deadline_ms() const noexcept;
        [[nodiscard]] task_returning_expected_void_t send_packet(
            cspan_uint8_t packet, endpoint_kind kind) noexcept(false);
        [[nodiscard]] datagram_task_t receive_datagram_impl() noexcept(false);
        [[nodiscard]] received_cemi_task_t receive_cemi_impl() noexcept(false);
        /// @brief Hands over a tunnelled frame, opened by Data Secure when a context is set; nothing when it was refused.
        [[nodiscard]] std::optional<received_cemi> deliver(const tunnelling_request_frame& request) const noexcept(false);
        /// @brief Indicates whether a refused frame is the previous one being retried by the server.
        [[nodiscard]] bool is_retransmission(const expected_void_t& accepted,
                                             const tunnelling_request_frame& request) const noexcept;
        /// @brief Checks the session is open and has not gone silent past its inactivity timeout.
        [[nodiscard]] expected_void_t check_session_live() noexcept;
        /// @brief Handles a datagram that is session bookkeeping rather than a tunnelled frame.
        [[nodiscard]] expected_void_t absorb_session_datagram(const datagram& value) noexcept;
        /// @brief Chooses which endpoint a service's datagram is expected to have arrived from.
        [[nodiscard]] endpoint_kind endpoint_for(std::uint16_t service_type) const noexcept;
        /// @brief Sends one datagram on the data channel.
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
        /// @brief Encodes the next TUNNELLING_REQUEST on the established channel.
        [[nodiscard]] std::expected<std::uint8_t, std::error_code> prepare_request(span_uint8_t packet, cspan_uint8_t cemi_bytes) noexcept;
        /// @brief Applies a received TUNNELLING_REQUEST, and tells whether it repeats the one accepted last.
        [[nodiscard]] indication_outcome accept_indication(const datagram& value, const tunnelling_request_frame& request) noexcept;
        /// @brief Starts a connect attempt and sees it through, retrying where the session allows.
        [[nodiscard]] task_returning_expected_void_t start_and_retry_connect(const connect_request_frame& request) noexcept(false);
        /// @brief Opens the connection and asks for a tunnel over it with TCP HPAIs.
        [[nodiscard]] task_returning_expected_void_t connect_stream(const connect_request_frame& request) noexcept(false);
        /// @brief Sends one TUNNELLING_REQUEST on a stream, where nothing acknowledges it.
        [[nodiscard]] task_returning_expected_void_t send_on_stream(cspan_uint8_t cemi_bytes) noexcept(false);
        /// @brief Sends one CONNECTIONSTATE_REQUEST on a stream, leaving its answer to the receive that is running.
        [[nodiscard]] task_returning_expected_void_t heartbeat_on_stream() noexcept(false);
        /// @brief Takes a receive's failure: on a stream, a connection that failed ends the tunnel and is closed.
        [[nodiscard]] std::error_code on_receive_failure(std::error_code failure) noexcept;
        /// @brief Records the IPv4 data endpoint a CONNECT_RESPONSE named, or the control endpoint in its place.
        [[nodiscard]] expected_void_t adopt_ipv4_data_endpoint(const hpai& endpoint) noexcept;
        /// @brief Stores and checks the configured peer, and picks the rules the transport calls for.
        void configure(const sockaddr* peer) noexcept;
        /// @brief Ends a secure session that has timed out, together with the tunnel inside it.
        [[nodiscard]] expected_void_t check_secure_session() noexcept;

        /// @brief The KNX IP Secure session the tunnel runs through, when there is one; @ref transport_ then refers to it.
        std::unique_ptr<secure::tunnel_transport> secure_ {};
        datagram_transport& transport_;
        /// @brief The Data Secure context applied to group telegrams, when one is set; see @ref use_data_secure.
        std::atomic<data_secure::context*> data_secure_ {};
        sockaddr_storage peer_ {};
        ::socklen_t peer_length_ = 0u;
        bool configured_peer_valid_ {};
        sockaddr_storage data_peer_ {};
        ::socklen_t data_peer_length_ = 0u;
        bool data_peer_valid_ {};
        clock_now_function clock_now_ {};
        std::atomic_bool operation_active_ {};
        /// @brief Guards @ref session_; see @ref with_session.
        mutable std::mutex session_mutex_ {};
        tunnelling_session session_;
        /// @brief Puts the sends of a stream transport on the wire one at a time.
        async_mutex send_mutex_ {};
        std::array<std::uint8_t, frame::max_datagram_size> receive_buffer_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
