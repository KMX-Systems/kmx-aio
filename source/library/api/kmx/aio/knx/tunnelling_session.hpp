/// @file api/kmx/aio/knx/tunnelling_session.hpp
/// @brief Lightweight KNX tunnelling session state machine.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// The tunnelling protocol's rules - which service is legal in which state, which channel and sequence
/// number an answer has to carry, when a retry becomes a give-up - with no I/O of any kind.
/// @ref kmx::aio::knx::tunnelling_session never sends, never receives, and never reads a clock: a caller
/// asks it to prepare a packet, sends those octets itself, and feeds back what arrived or that a deadline
/// passed. Time enters only as the `now_ms` and `deadline_ms` arguments.
///
/// That separation is what makes the protocol testable. Every transition here can be driven from a test
/// without a socket, and @ref kmx::aio::knx::tunnelling_client is then a thin layer that owns a transport
/// and a clock and does nothing else.
///
/// The methods fall into three groups, which is the order a caller uses them in:
/// - `prepare_*` and `start_*` build a packet to send, and move the machine into the state that expects an
///   answer. The packet is also retained, so a retry re-sends the identical octets.
/// - `on_*` feed back what happened: a decoded frame, raw octets, or a timeout.
/// - the observers - @ref kmx::aio::knx::tunnelling_session::state,
///   @ref kmx::aio::knx::tunnelling_session::expired and their neighbours - say what to do next.
/// @reference KNX System Specifications, 03/08/04 "Tunnelling".
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/connection.hpp>
        #include <kmx/aio/knx/datagram.hpp>
        #include <kmx/aio/knx/frame.hpp>
        #include <kmx/aio/knx/individual_address.hpp>
        #include <kmx/aio/knx/session.hpp>

        #include <cstdint>
        #include <expected>
        #include <system_error>
    #endif

namespace kmx::aio::knx
{
    /// @brief The tunnelling protocol's state machine, without any I/O.
    /// @details Holds the channel id, both sequence counters, the retry counts and the last packet sent,
    ///          and enforces the rules that relate them. It does not own a socket or a clock; see the file
    ///          documentation for how a caller drives it.
    /// @note Two sequence counters are tracked, not one. The outgoing counter numbers this side's requests;
    ///       the incoming one follows the peer's, which is what lets a retransmitted indication be told
    ///       from a new one - see @ref duplicate_indication and @ref out_of_order_indication.
    class tunnelling_session
    {
    public:
        /// @brief Creates an idle session with the default timing policy.
        constexpr tunnelling_session() noexcept = default;

        /// @brief Creates an idle session.
        /// @param cfg The timing and retry policy to apply.
        constexpr explicit tunnelling_session(const tunnelling_config& cfg) noexcept: config_(cfg) {}

        /// @brief Returns the session to its initial state, keeping the configuration.
        /// @note The only way out of @ref session_state::closed. Everything else - channel, sequence
        ///       counters, retry counts, retained packets - is cleared.
        constexpr void reset() noexcept
        {
            state_ = session_state::idle;
            pending_ = false;
            connect_retries_ = 0u;
            disconnect_retries_ = 0u;
            retries_ = 0u;
            channel_id_ = 0u;
            expected_sequence_ = 0u;
            next_sequence_ = 0u;
            heartbeat_failures_ = 0u;
            last_activity_ms_ = 0u;
            incoming_sequence_valid_ = false;
            last_incoming_sequence_ = 0u;
            next_incoming_sequence_ = 0u;
            assigned_address_ = individual_address {};
            request_packet_.clear();
            connect_packet_.clear();
            last_deadline_ms_ = 0u;
            heartbeat_outstanding_ = false;
            heartbeat_deadline_ms_ = 0u;
            control_endpoint_ = hpai {};
        }

        /// @brief Returns where the session is in its lifecycle.
        [[nodiscard]] constexpr session_state state() const noexcept { return state_; }

        /// @brief Indicates whether a request is awaiting its acknowledgement.
        [[nodiscard]] constexpr bool has_pending_request() const noexcept { return pending_; }

        /// @brief Returns how many times the pending request has been re-sent.
        [[nodiscard]] constexpr std::uint32_t retries() const noexcept { return retries_; }

        /// @brief Returns the channel the server allocated; zero before a connection is established.
        [[nodiscard]] constexpr std::uint16_t channel_id() const noexcept { return channel_id_; }

        /// @brief Returns the sequence number the outstanding acknowledgement must carry.
        [[nodiscard]] constexpr std::uint16_t expected_sequence() const noexcept { return expected_sequence_; }

        /// @brief Returns the sequence number the next request will use.
        [[nodiscard]] constexpr std::uint8_t next_sequence() const noexcept { return next_sequence_; }

        /// @brief Returns how many consecutive heartbeats have failed; reset by a successful one.
        [[nodiscard]] constexpr std::uint8_t heartbeat_failures() const noexcept { return heartbeat_failures_; }

        /// @brief Returns when traffic was last seen, on the caller's clock.
        [[nodiscard]] constexpr std::uint32_t last_activity_ms() const noexcept { return last_activity_ms_; }

        /// @brief Returns how long to wait for a TUNNELLING_ACK.
        [[nodiscard]] constexpr std::uint32_t ack_timeout_ms() const noexcept { return config_.ack_timeout_ms; }

        /// @brief Returns how long to wait for a CONNECT_RESPONSE.
        [[nodiscard]] constexpr std::uint32_t connect_timeout_ms() const noexcept { return config_.connect_timeout_ms; }

        /// @brief Returns how long to wait for a CONNECTIONSTATE_RESPONSE.
        [[nodiscard]] constexpr std::uint32_t connectionstate_timeout_ms() const noexcept { return config_.connectionstate_timeout_ms; }

        /// @brief Returns how long to wait for a DISCONNECT_RESPONSE.
        [[nodiscard]] constexpr std::uint32_t disconnect_timeout_ms() const noexcept { return config_.disconnect_timeout_ms; }

        /// @brief Returns how often a supervisor should send a heartbeat.
        [[nodiscard]] constexpr std::uint32_t heartbeat_interval_ms() const noexcept { return config_.heartbeat_interval_ms; }

        /// @brief Selects the rules of KNXnet/IP over TCP, or those of KNXnet/IP over UDP.
        /// @details A stream already delivers every frame once and in order, so under its rules:
        ///          - no TUNNELLING_ACK is sent or awaited: a request is complete once prepared, and the send sequence
        ///            still advances;
        ///          - the peer's sequence numbers are not checked;
        ///          - a connect or a disconnect is attempted once, since resending it on the same connection gains nothing.
        /// @note Kept by @ref reset: it describes the transport, which a reset does not change.
        constexpr void use_stream_rules(const bool stream) noexcept { stream_ = stream; }

        /// @brief Indicates whether the rules of KNXnet/IP over TCP apply.
        [[nodiscard]] constexpr bool stream_rules() const noexcept { return stream_; }

        /// @brief Indicates whether a heartbeat sent without waiting still has no answer.
        [[nodiscard]] constexpr bool heartbeat_outstanding() const noexcept { return heartbeat_outstanding_; }

        /// @brief Returns the deadline the outstanding exchange was given, on the caller's clock.
        /// @note Compare against it with @ref expired or @ref connect_expired rather than directly; those
        ///       account for the counter wrapping.
        [[nodiscard]] constexpr std::uint32_t deadline_ms() const noexcept { return last_deadline_ms_; }

        /// @brief Returns the individual address the interface assigned to this tunnel.
        /// @note Unset until a CONNECT_RESPONSE has been accepted.
        [[nodiscard]] constexpr individual_address assigned_address() const noexcept { return assigned_address_; }

        /// @brief Returns the retained octets of the outstanding request, for a retry.
        /// @note Empty when nothing is pending. A retry must re-send these exact octets - a re-encoded
        ///       request would carry a new sequence number and would not be a retransmission.
        [[nodiscard]] cspan_uint8_t active_request_packet() const noexcept { return {request_packet_.data(), request_packet_.size()}; }

        /// @brief Returns the retained octets of the outstanding CONNECT_REQUEST, for a retry.
        /// @note Empty unless the session is connecting.
        [[nodiscard]] cspan_uint8_t active_connect_packet() const noexcept { return {connect_packet_.data(), connect_packet_.size()}; }

        /// @brief Marks the session closed and drops the retained packets.
        /// @note Local only - nothing is sent. This is what a completed disconnect leaves behind, and what
        ///       a caller uses to abandon a session whose transport has already failed.
        constexpr void shutdown() noexcept
        {
            pending_ = false;
            heartbeat_outstanding_ = false;
            state_ = session_state::closed;
            request_packet_.clear();
            connect_packet_.clear();
        }

        /// @brief Encodes a CONNECT_REQUEST and retains it, without changing state.
        /// @param packet The destination octets.
        /// @param request The connection to ask for.
        /// @return Nothing, or the reason no request could be prepared.
        /// @retval kmx::aio::knx::error::invalid_configuration The session is not idle.
        /// @note @ref start_connect is the usual entry point; this exists for a caller that wants the
        ///       octets before committing the session to the attempt.
        [[nodiscard]] expected_void_t prepare_connect_request_packet(const span_uint8_t packet,
                                                                     const connect_request_frame& request) noexcept;

        /// @brief Encodes a CONNECT_REQUEST, retains it, and enters @ref session_state::connecting.
        /// @param packet The destination octets, to be sent by the caller.
        /// @param request The connection to ask for.
        /// @param deadline_ms When to give up on the response, on the caller's clock.
        /// @return Nothing, or the reason the attempt could not start.
        /// @retval kmx::aio::knx::error::invalid_configuration The session is not idle.
        [[nodiscard]] expected_void_t start_connect(const span_uint8_t packet, const connect_request_frame& request,
                                                    const std::uint32_t deadline_ms) noexcept;

        /// @brief Enters @ref session_state::connecting around a CONNECT_REQUEST the caller encoded itself.
        /// @param packet The complete request datagram, retained for retries.
        /// @param deadline_ms When to give up on the response, on the caller's clock.
        /// @return Nothing, or the reason the attempt could not start.
        /// @retval kmx::aio::knx::error::invalid_configuration The session is not idle.
        /// @retval kmx::aio::knx::error::invalid_length @p packet is shorter than a KNXnet/IP header or
        ///         longer than a datagram.
        /// @note The escape hatch for a request this header cannot express - an IPv6 or a secure-wrapped
        ///       one. The octets are taken as given and never re-encoded.
        [[nodiscard]] expected_void_t start_connect_raw(const cspan_uint8_t packet, const std::uint32_t deadline_ms) noexcept;

        /// @brief Copies the retained CONNECT_REQUEST out for a retry.
        /// @param packet The destination octets.
        /// @return Nothing, or the reason no retry is available.
        /// @retval kmx::aio::knx::error::invalid_configuration The session is not connecting, or
        ///         @ref on_connect_timeout has not yet counted a timeout.
        /// @retval kmx::aio::knx::error::invalid_length @p packet is smaller than the retained request.
        [[nodiscard]] expected_void_t prepare_connect_retry_packet(const span_uint8_t packet) const noexcept;

        /// @brief Marks a request outstanding at an explicit channel and sequence.
        /// @param channel_id The channel the request is on; must be non-zero and fit an octet.
        /// @param sequence The sequence number the request carries; must fit an octet.
        /// @param deadline_ms When to give up on the acknowledgement, on the caller's clock.
        /// @return Nothing, or the reason the request could not be started.
        /// @retval kmx::aio::knx::error::send_queue_full A request is already outstanding; tunnelling
        ///         allows only one at a time.
        /// @retval kmx::aio::knx::error::shutdown The session is closing or closed.
        /// @retval kmx::aio::knx::error::sequence_error The channel or sequence is out of range.
        [[nodiscard]] expected_void_t begin_request(const std::uint16_t channel_id, const std::uint16_t sequence,
                                                    const std::uint32_t deadline_ms) noexcept;

        /// @brief Marks a request outstanding at the session's own next sequence number.
        /// @param channel_id The channel the request is on; must be non-zero.
        /// @param deadline_ms When to give up on the acknowledgement, on the caller's clock.
        /// @return The sequence number allocated, or the reason the request could not be started.
        /// @note Fails for the same reasons as the explicit-sequence overload.
        [[nodiscard]] std::expected<std::uint8_t, std::error_code> begin_request(const std::uint8_t channel_id,
                                                                                 const std::uint32_t deadline_ms) noexcept;

        /// @brief Encodes a TUNNELLING_REQUEST, retains it, and marks it outstanding.
        /// @param packet The destination octets, to be sent by the caller.
        /// @param channel_id The channel to send on; must be non-zero.
        /// @param cemi_bytes The cEMI frame to tunnel.
        /// @param deadline_ms When to give up on the acknowledgement, on the caller's clock.
        /// @return The sequence number the request carries, or the reason it could not be prepared.
        /// @retval kmx::aio::knx::error::send_queue_full A request is already outstanding.
        /// @retval kmx::aio::knx::error::shutdown The session is closing or closed.
        /// @retval kmx::aio::knx::error::sequence_error @p channel_id is zero.
        /// @note The ordinary way to send. The sequence number is allocated here, so a caller never picks
        ///       one itself. Under @ref use_stream_rules the request is complete once prepared: nothing is retained
        ///       for a retry, and nothing is left outstanding.
        [[nodiscard]] std::expected<std::uint8_t, std::error_code> prepare_request_packet(const span_uint8_t packet,
                                                                                          const std::uint8_t channel_id,
                                                                                          const cspan_uint8_t cemi_bytes,
                                                                                          const std::uint32_t deadline_ms) noexcept;

        /// @brief Copies the retained TUNNELLING_REQUEST out for a retry.
        /// @param packet The destination octets.
        /// @return Nothing, or the reason no retry is available.
        /// @retval kmx::aio::knx::error::invalid_configuration Nothing is outstanding, or @ref on_timeout
        ///         has not yet counted a timeout.
        /// @retval kmx::aio::knx::error::invalid_length @p packet is smaller than the retained request.
        [[nodiscard]] expected_void_t prepare_retry_packet(const span_uint8_t packet) const noexcept;

        /// @brief Encodes a CONNECTIONSTATE_REQUEST for the established channel.
        /// @details The request names the control endpoint the connect named - the TCP HPAI over a stream. After
        ///          @ref start_connect_raw, whose endpoints are not read back, it names the route-back HPAI.
        /// @param packet The destination octets.
        /// @return Nothing, or the reason it could not be prepared.
        /// @retval kmx::aio::knx::error::shutdown The session is not connected.
        /// @note Does not change state; a heartbeat is not an outstanding request in the sense
        ///       @ref has_pending_request means, so it does not block ordinary traffic.
        [[nodiscard]] expected_void_t prepare_connectionstate_request_packet(const span_uint8_t packet) const noexcept;

        /// @brief Encodes a DISCONNECT_REQUEST and enters @ref session_state::closing.
        /// @details The request carries the control endpoint, as @ref prepare_connectionstate_request_packet does.
        /// @param packet The destination octets.
        /// @return Nothing, or the reason it could not be prepared.
        /// @retval kmx::aio::knx::error::shutdown The session is already closed.
        /// @retval kmx::aio::knx::error::invalid_configuration The session is not connected.
        [[nodiscard]] expected_void_t prepare_disconnect_request_packet(const span_uint8_t packet) noexcept;

        /// @brief Reports that the DISCONNECT_RESPONSE did not arrive in time.
        /// @return Nothing when another attempt is allowed, otherwise the reason the session gave up.
        /// @retval kmx::aio::knx::error::invalid_configuration The session is not closing.
        /// @retval kmx::aio::knx::error::timeout The retry budget is spent; the session is now closed.
        /// @note Closing on a spent budget is correct rather than a leak: the peer has stopped answering,
        ///       so there is nothing further a client can do to close the channel politely.
        [[nodiscard]] expected_void_t on_disconnect_timeout() noexcept;

        /// @brief Encodes the TUNNELLING_ACK a received request expects.
        /// @param packet The destination octets.
        /// @param request The decoded request being acknowledged.
        /// @param status The status to report; zero for success.
        /// @return Nothing, or the reason no acknowledgement could be built.
        /// @retval kmx::aio::knx::error::malformed_frame @p request has no channel or carries no cEMI.
        /// @note The channel and sequence are echoed from @p request, which is what makes it an
        ///       acknowledgement of that request rather than of the session's own counters.
        [[nodiscard]] expected_void_t prepare_tunnelling_ack_packet(const span_uint8_t packet, const tunnelling_request_frame& request,
                                                                    const std::uint8_t status = 0u) const noexcept;

        /// @brief Decodes a received request and encodes the TUNNELLING_ACK it expects.
        /// @param packet The destination octets.
        /// @param request_packet The received request datagram.
        /// @param status The status to report; zero for success.
        /// @return Nothing, or the reason no acknowledgement could be built.
        [[nodiscard]] expected_void_t prepare_tunnelling_ack_packet(const span_uint8_t packet, const cspan_uint8_t request_packet,
                                                                    const std::uint8_t status = 0u) const noexcept;

        /// @brief Encodes the answer a received request expects, and applies the request to the session.
        /// @param packet The destination octets.
        /// @param request The decoded request being answered.
        /// @param status The status to report; zero for success.
        /// @return Nothing, or the reason the request was rejected or could not be answered.
        /// @retval kmx::aio::knx::error::invalid_configuration @p request carries a service type its frame
        ///         does not belong to, or arrived in a state that does not accept it.
        /// @retval kmx::aio::knx::error::sequence_error @p request is for another channel, or is a
        ///         duplicate or out-of-order indication.
        /// @details The answer is encoded first and the state change applied only if the request is
        ///          accepted, so a rejected request leaves the session untouched. This is the call that
        ///          both answers a peer and advances the incoming sequence counter; answering without it
        ///          would leave the session unable to tell the next retransmission from a new frame.
        [[nodiscard]] expected_void_t prepare_response_datagram(const span_uint8_t packet, const datagram& request,
                                                                const std::uint8_t status = 0u) noexcept;

        /// @brief Decodes a received request, answers it, and applies it to the session.
        /// @param packet The destination octets.
        /// @param request_packet The received request datagram.
        /// @param status The status to report; zero for success.
        /// @return Nothing, or the reason the request was rejected or could not be answered.
        [[nodiscard]] expected_void_t prepare_response_datagram(const span_uint8_t packet, const cspan_uint8_t request_packet,
                                                                const std::uint8_t status = 0u) noexcept;

        /// @brief Accepts an acknowledgement identified by channel and sequence.
        /// @param channel_id The channel the acknowledgement names.
        /// @param sequence The sequence number it names.
        /// @return Nothing, or the reason it was rejected.
        /// @retval kmx::aio::knx::error::sequence_error Nothing is outstanding, or the acknowledgement is
        ///         for another channel or another request.
        /// @note On success the session returns to @ref session_state::connected and the retry count is
        ///       cleared.
        [[nodiscard]] expected_void_t on_ack(const std::uint16_t channel_id, const std::uint16_t sequence) noexcept;

        /// @brief Accepts a decoded TUNNELLING_ACK.
        /// @param ack The acknowledgement received.
        /// @return Nothing, or the reason it was rejected.
        /// @retval kmx::aio::knx::error::connection_failed The acknowledgement reports a non-zero status,
        ///         so the request was refused rather than accepted.
        [[nodiscard]] expected_void_t on_ack(const tunnelling_ack_frame& ack) noexcept;

        /// @brief Accepts a CONNECT_RESPONSE, establishing the channel.
        /// @param response The response received.
        /// @return Nothing, or the reason the connection was not established.
        /// @retval kmx::aio::knx::error::invalid_configuration The session is neither idle nor connecting.
        /// @retval kmx::aio::knx::error::connection_failed The server refused; see
        ///         @ref connect_response_frame::status.
        /// @retval kmx::aio::knx::error::sequence_error The server accepted but allocated channel zero.
        /// @note A refusal returns a connecting session to @ref session_state::idle, so the caller may
        ///       attempt a fresh connection without resetting first.
        [[nodiscard]] expected_void_t on_connect_response(const connect_response_frame& response) noexcept;

        /// @brief Accepts an IPv6 CONNECT_RESPONSE, establishing the channel.
        /// @param response The response received.
        /// @return Nothing, or the reason the connection was not established.
        /// @note The data endpoint is not retained; the session tracks only the channel and the address.
        [[nodiscard]] expected_void_t on_connect_response(const ipv6_connect_response_frame& response) noexcept;

        /// @brief Indicates whether a request repeats the sequence number of the last one accepted.
        /// @param request The request received.
        /// @return `true` when it is a retransmission of the frame already handled.
        /// @note A duplicate must still be acknowledged - the peer resent it because it missed the first
        ///       acknowledgement - but its payload must not be delivered a second time.
        [[nodiscard]] constexpr bool duplicate_indication(const tunnelling_request_frame& request) const noexcept
        {
            return !stream_ && (state_ == session_state::connected) && (request.channel_id == channel_id_) && incoming_sequence_valid_ &&
                   (request.sequence_number == last_incoming_sequence_);
        }

        /// @brief Indicates whether a request skips the sequence number expected next.
        /// @param request The request received.
        /// @return `true` when its sequence number is neither the expected one nor a duplicate.
        /// @note Always `false` until a first indication has been accepted, since there is nothing to be
        ///       out of order with respect to.
        [[nodiscard]] constexpr bool out_of_order_indication(const tunnelling_request_frame& request) const noexcept
        {
            return !stream_ && incoming_sequence_valid_ && (request.sequence_number != next_incoming_sequence_);
        }

        /// @brief Records that traffic was seen, deferring the inactivity timeout.
        /// @param now_ms The current time on the caller's clock.
        /// @note Ignored unless the session is connected.
        constexpr void note_activity(const std::uint32_t now_ms) noexcept
        {
            if (state_ == session_state::connected)
                last_activity_ms_ = now_ms;
        }

        /// @brief Records that traffic was seen.
        /// @param now_ms The current time on the caller's clock.
        /// @note A spelling of @ref note_activity, matching the server's vocabulary.
        constexpr void observe_activity(const std::uint32_t now_ms) noexcept { note_activity(now_ms); }

        /// @brief Indicates whether a connected session has been silent past its inactivity timeout.
        /// @param now_ms The current time on the caller's clock.
        /// @return `true` when the session should be closed as dead.
        /// @note The comparison is made on a signed difference, so it stays correct across the wrap of the
        ///       millisecond counter.
        [[nodiscard]] constexpr bool inactive(const std::uint32_t now_ms) const noexcept
        {
            return (state_ == session_state::connected) &&
                   (static_cast<std::int32_t>(now_ms - last_activity_ms_) >= static_cast<std::int32_t>(config_.inactivity_timeout_ms));
        }

        /// @brief Closes the session if it has been silent past its inactivity timeout.
        /// @param now_ms The current time on the caller's clock.
        /// @return Nothing while the session is alive.
        /// @retval kmx::aio::knx::error::inactivity_timeout The session was silent too long and is now
        ///         closed.
        [[nodiscard]] expected_void_t check_inactivity(const std::uint32_t now_ms) noexcept;

        /// @brief Decodes a CONNECT_RESPONSE datagram and applies it.
        /// @param packet The received datagram.
        /// @return Nothing, or the reason the connection was not established.
        [[nodiscard]] expected_void_t on_connect_response_packet(const cspan_uint8_t packet) noexcept;

        /// @brief Reports that the CONNECT_RESPONSE did not arrive in time.
        /// @return Nothing when another attempt is allowed, otherwise the reason the attempt failed.
        /// @retval kmx::aio::knx::error::invalid_configuration The session is not connecting.
        /// @retval kmx::aio::knx::error::timeout The retry budget is spent; the session is now closed.
        /// @note On a retry the deadline advances by one connect timeout, so
        ///       @ref prepare_connect_retry_packet and @ref deadline_ms stay consistent.
        [[nodiscard]] expected_void_t on_connect_timeout() noexcept;

        /// @brief Indicates whether a connect attempt has passed its deadline.
        /// @param now_ms The current time on the caller's clock.
        /// @return `true` when @ref on_connect_timeout should be called.
        [[nodiscard]] constexpr bool connect_expired(const std::uint32_t now_ms) const noexcept
        {
            return (state_ == session_state::connecting) && (static_cast<std::int32_t>(now_ms - last_deadline_ms_) >= 0);
        }

        /// @brief Accepts a CONNECTIONSTATE_RESPONSE, clearing or counting a heartbeat failure.
        /// @param response The response received.
        /// @return Nothing when the channel is confirmed alive, otherwise why it was not.
        /// @retval kmx::aio::knx::error::invalid_configuration The session is not connected.
        /// @retval kmx::aio::knx::error::sequence_error The response names another channel.
        /// @retval kmx::aio::knx::error::connection_failed The server reported a problem, but the failure
        ///         limit has not been reached; the session stays connected.
        /// @retval kmx::aio::knx::error::heartbeat_failed The failure limit has been reached; the session
        ///         is now closed.
        [[nodiscard]] expected_void_t on_connectionstate_response(const connectionstate_response_frame& response) noexcept;

        /// @brief Decodes a CONNECTIONSTATE_RESPONSE datagram and applies it.
        /// @param packet The received datagram.
        /// @return Nothing, or the reason the heartbeat did not succeed.
        [[nodiscard]] expected_void_t on_connectionstate_response_packet(const cspan_uint8_t packet) noexcept;

        /// @brief Reports that a CONNECTIONSTATE_RESPONSE did not arrive in time.
        /// @return Never nothing; a missed heartbeat is always a failure of some degree.
        /// @retval kmx::aio::knx::error::invalid_configuration The session is not connected.
        /// @retval kmx::aio::knx::error::connection_failed Counted, but under the failure limit.
        /// @retval kmx::aio::knx::error::heartbeat_failed The limit has been reached; the session is now
        ///         closed.
        [[nodiscard]] expected_void_t on_connectionstate_timeout() noexcept;

        /// @brief Records that a CONNECTIONSTATE_REQUEST went out with nothing waiting for its answer.
        /// @param deadline_ms When the answer is due, on the caller's clock.
        /// @details Whatever receive is running applies the answer, which clears this; @ref check_heartbeat counts the
        ///          heartbeat as failed if @p deadline_ms passes first. A heartbeat sent while one is outstanding keeps
        ///          the earlier deadline, so a silent peer is noticed on schedule.
        constexpr void note_heartbeat_sent(const std::uint32_t deadline_ms) noexcept
        {
            if (heartbeat_outstanding_)
                return;
            heartbeat_outstanding_ = true;
            heartbeat_deadline_ms_ = deadline_ms;
        }

        /// @brief Counts an unanswered heartbeat as failed once its deadline has passed.
        /// @param now_ms The current time on the caller's clock.
        /// @return Nothing while no heartbeat is overdue.
        /// @retval kmx::aio::knx::error::connection_failed The heartbeat went unanswered, under the failure limit.
        /// @retval kmx::aio::knx::error::heartbeat_failed The failure limit has been reached; the session is now closed.
        [[nodiscard]] expected_void_t check_heartbeat(std::uint32_t now_ms) noexcept;

        /// @brief Decodes a TUNNELLING_ACK datagram and applies it.
        /// @param packet The received datagram.
        /// @return Nothing, or the reason it was rejected.
        [[nodiscard]] expected_void_t on_ack_packet(const cspan_uint8_t packet) noexcept;

        /// @brief Decodes a DISCONNECT_RESPONSE datagram and closes the session.
        /// @param packet The received datagram.
        /// @return Nothing, or the reason the close was not accepted.
        /// @retval kmx::aio::knx::error::shutdown The session is already closed.
        /// @retval kmx::aio::knx::error::invalid_configuration The session is not closing.
        /// @retval kmx::aio::knx::error::sequence_error The response names another channel.
        /// @retval kmx::aio::knx::error::connection_failed The response reports a non-zero status.
        [[nodiscard]] expected_void_t on_disconnect_response_packet(const cspan_uint8_t packet) noexcept;

        /// @brief Applies any decoded session datagram, dispatching on what it is.
        /// @param value The decoded datagram.
        /// @return Nothing, or the reason it was rejected.
        /// @retval kmx::aio::knx::error::invalid_configuration The service type does not match the decoded
        ///         frame, or the frame arrived in a state that does not accept it.
        /// @retval kmx::aio::knx::error::sequence_error The frame is for another channel, or is a duplicate
        ///         or out-of-order indication.
        /// @retval kmx::aio::knx::error::unsupported_service The frame is not part of a tunnelling session.
        /// @note Does not defer the inactivity timeout; @ref dispatch_session_datagram does both.
        [[nodiscard]] expected_void_t on_datagram(const datagram& value) noexcept;

        /// @brief Applies a decoded datagram and, if accepted, records the activity.
        /// @param value The decoded datagram.
        /// @param now_ms The current time on the caller's clock.
        /// @return Nothing, or the reason it was rejected.
        /// @note The activity is recorded only on acceptance, so a rejected frame cannot keep a session
        ///       that is receiving nothing valid alive past its inactivity timeout.
        [[nodiscard]] expected_void_t dispatch_session_datagram(const datagram& value, const std::uint32_t now_ms) noexcept;

        /// @brief Decodes a datagram, applies it, and records the activity.
        /// @param packet The received datagram.
        /// @param now_ms The current time on the caller's clock.
        /// @return Nothing, or the reason it was rejected.
        [[nodiscard]] expected_void_t dispatch_session_datagram(const cspan_uint8_t packet, const std::uint32_t now_ms) noexcept;

        /// @brief Decodes a datagram and applies it, without recording activity.
        /// @param packet The received datagram.
        /// @return Nothing, or the reason it was rejected.
        [[nodiscard]] expected_void_t on_datagram(const cspan_uint8_t packet) noexcept;

        /// @brief Applies a decoded datagram and records the activity.
        /// @param value The decoded datagram.
        /// @param now_ms The current time on the caller's clock.
        /// @return Nothing, or the reason it was rejected.
        /// @note A spelling of @ref dispatch_session_datagram.
        [[nodiscard]] expected_void_t on_datagram_at(const datagram& value, const std::uint32_t now_ms) noexcept
        {
            return dispatch_session_datagram(value, now_ms);
        }

        /// @brief Decodes a datagram, applies it, and records the activity.
        /// @param packet The received datagram.
        /// @param now_ms The current time on the caller's clock.
        /// @return Nothing, or the reason it was rejected.
        /// @note A spelling of @ref dispatch_session_datagram.
        [[nodiscard]] expected_void_t on_datagram_at(const cspan_uint8_t packet, const std::uint32_t now_ms) noexcept
        {
            return dispatch_session_datagram(packet, now_ms);
        }

        /// @brief Reports that the TUNNELLING_ACK did not arrive in time.
        /// @return Nothing when another attempt is allowed, otherwise the reason the request failed.
        /// @retval kmx::aio::knx::error::invalid_configuration Nothing is outstanding.
        /// @retval kmx::aio::knx::error::timeout The retry budget is spent; the session is now closed.
        /// @note On a retry the deadline advances by one acknowledgement timeout, and the retained packet
        ///       becomes available through @ref prepare_retry_packet.
        [[nodiscard]] expected_void_t on_timeout() noexcept;

        /// @brief Indicates whether the outstanding request has passed its deadline.
        /// @param now_ms The current time on the caller's clock.
        /// @return `true` when @ref on_timeout should be called.
        [[nodiscard]] constexpr bool expired(const std::uint32_t now_ms) const noexcept
        {
            return pending_ && (static_cast<std::int32_t>(now_ms - last_deadline_ms_) >= 0);
        }

    private:
        /// @brief Rewinds a connect attempt that was refused, leaving the session idle.
        void abandon_connect() noexcept;
        /// @brief Acts on a TUNNELLING_REQUEST that arrived on this session's channel.
        [[nodiscard]] expected_void_t on_indication(const tunnelling_request_frame& request) noexcept;
        /// @brief Acts on a peer's CONNECTIONSTATE_REQUEST heartbeat.
        [[nodiscard]] expected_void_t on_connectionstate_request(const connectionstate_request_frame& request) noexcept;
        /// @brief Acts on a peer's DISCONNECT_REQUEST, moving the session to closing.
        [[nodiscard]] expected_void_t on_disconnect_request(const disconnect_request_frame& request) noexcept;
        /// @brief Acts on the DISCONNECT_RESPONSE that completes a close this session started.
        [[nodiscard]] expected_void_t on_disconnect_response(const disconnect_response_frame& response) noexcept;
        /// @brief Handles the datagrams that answer something this session sent.
        [[nodiscard]] optional_expected_void_t on_response_datagram(const datagram& value) noexcept;
        /// @brief Handles the datagrams a peer sends unprompted.
        [[nodiscard]] optional_expected_void_t on_request_datagram(const datagram& value) noexcept;
        tunnelling_config config_ {};
        session_state state_ = session_state::idle;
        bool pending_ {};
        std::uint32_t retries_ {};
        std::uint16_t channel_id_ {};
        std::uint16_t expected_sequence_ {};
        std::uint8_t next_sequence_ {};
        std::uint16_t connect_retries_ {};
        std::uint16_t disconnect_retries_ {};
        std::uint8_t heartbeat_failures_ {};
        std::uint32_t last_activity_ms_ {};
        bool incoming_sequence_valid_ {};
        std::uint8_t last_incoming_sequence_ {};
        std::uint8_t next_incoming_sequence_ {};
        individual_address assigned_address_ {};
        byte_buffer_t request_packet_ {};
        byte_buffer_t connect_packet_ {};
        std::uint32_t last_deadline_ms_ {};
        bool stream_ {};
        bool heartbeat_outstanding_ {};
        std::uint32_t heartbeat_deadline_ms_ {};
        /// @brief The control endpoint the connect named, which heartbeats and disconnects carry.
        hpai control_endpoint_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
