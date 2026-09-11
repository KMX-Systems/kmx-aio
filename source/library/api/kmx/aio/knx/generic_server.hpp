/// @file api/kmx/aio/knx/generic_server.hpp
/// @brief Executor-neutral KNXnet/IP tunnelling server.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// The other side of @ref kmx::aio::knx::tunnelling_client: it accepts connections, answers discovery and
/// description, allocates a channel and an individual address per client, and hands the cEMI frames that
/// arrive up to the application as @ref kmx::aio::knx::server_event.
///
/// What it does not do is decide what those frames mean. A KNXnet/IP server is a doorway onto a bus, and
/// which bus - a real TP interface, a simulator, another gateway - is the application's business, so this
/// class carries the protocol and stops there. @ref kmx::aio::knx::generic_server::send is the way back:
/// a frame from the bus is sent onto whichever channel should see it.
///
/// Discovery is answered only when the server has been told its own control endpoint, because a search
/// response has to name an address to reach the server at and a build that does not know its own cannot
/// answer usefully. See @ref kmx::aio::knx::server_config::control_endpoint.
///
/// Tunnelling over TCP runs through the same server. Each connection is served by
/// @ref kmx::aio::knx::generic_server::serve_connection, usually from a task an accept loop spawned for it - see
/// `readiness/knx/tcp_server.hpp` and `completion/knx/tcp_server.hpp`. A channel opened over a connection is answered on
/// that connection and released when it ends, and over TCP nothing is acknowledged and no sequence number is checked.
/// Those loops, the datagram loop and @ref kmx::aio::knx::generic_server::send may run at once on different threads, so
/// the channel table is guarded by a lock held for bookkeeping alone, never across network I/O.
///
/// Configured with @ref kmx::aio::knx::server_config::secure, the server tunnels inside KNX IP Secure sessions alone,
/// which are offered over TCP. On each connection it answers SESSION_REQUEST, authenticates users, and opens and seals
/// every wrapper. A channel opened in a session answers through that session, and is given only a tunnel address its user
/// may use. An unencrypted CONNECT_REQUEST is refused with E_CONNECTION_TYPE before any channel is allocated (P1), every
/// other unencrypted frame but discovery is dropped, and a session that goes quiet is ended with its channels.
/// @reference KNX System Specifications, 03/08/02 "Core", 03/08/04 "Tunnelling" and 03/08/09 "KNXnet/IP Security".
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/connection.hpp>
        #include <kmx/aio/knx/datagram.hpp>
        #include <kmx/aio/knx/datagram_transport.hpp>
        #include <kmx/aio/knx/dib.hpp>
        #include <kmx/aio/knx/dib/supported_service_families.hpp>
        #include <kmx/aio/knx/discovery.hpp>
        #include <kmx/aio/knx/frame.hpp>
        #include <kmx/aio/knx/individual_address.hpp>
        #include <kmx/aio/knx/secure/common.hpp>
        #include <kmx/aio/knx/secure/entropy_source.hpp>
        #include <kmx/aio/knx/secure/server_configuration.hpp>
        #include <kmx/aio/knx/secure/session.hpp>
        #include <kmx/aio/knx/secure/wrapper.hpp>
        #include <kmx/aio/knx/server.hpp>
        #include <kmx/aio/knx/transport.hpp>
        #include <kmx/aio/mac.hpp>
        #include <kmx/aio/task.hpp>

        #include <array>
        #include <atomic>
        #include <cstddef>
        #include <cstdint>
        #include <expected>
        #include <memory>
        #include <mutex>
        #include <optional>
        #include <span>
        #include <system_error>
        #include <type_traits>
        #include <utility>
        #include <vector>
        #include <sys/socket.h>
    #endif

namespace kmx::aio::knx::secure
{
    class server_session_table;
    class session_link;
}

namespace kmx::aio::knx
{
    /// @brief A monotonic millisecond clock; null selects the steady clock.
    using server_clock_now_function = std::uint32_t (*)() noexcept;

    /// @brief What a tunnelling server offers and how it identifies itself.
    struct server_config
    {
        /// @brief How many tunnelling connections may be open at once.
        /// @note Channel identifiers run from 1, so the usable ceiling is 255 whatever is set here.
        std::uint8_t max_channels = 16u;
        /// @brief The individual address given to the first channel; later channels count up from it.
        /// @details Every tunnelling client needs its own address on the bus, and the server hands them
        ///          out. The range starting here must be one no real device on the installation occupies. A
        ///          client asking for an address in the extended CRI is given the channel that carries it, or
        ///          refused when that channel is taken or the address is outside the range.
        /// @note Not used by a secure server, which gives each user the tunnel addresses listed for it.
        individual_address first_assigned_address {1u, 1u, 1u};
        /// @brief How long a channel may see no traffic before the server reclaims it.
        /// @note Zero disables reclamation, which leaves a client that vanished holding its channel until
        ///       the server is reset.
        std::uint32_t inactivity_timeout_ms = 120'000u;
        /// @brief The control endpoint this server advertises in SEARCH and DESCRIPTION responses.
        /// @details A server that answers a search has to say where to reach it, and the answer travels to
        ///          the requester's own discovery endpoint rather than back along the multicast group. Left
        ///          at its default - protocol zero - the server does not answer discovery at all, which is
        ///          the behaviour of a build that has not been told its own address.
        hpai control_endpoint {ipv4_endpoint {}, 0x00u};
        /// @brief The description this server reports, as typed blocks.
        /// @details Encoded in order ahead of @ref device_info_blocks. A supported-service-families block
        ///          here is also what a SEARCH_REQUEST_EXTENDED selecting by service is matched against, so
        ///          the server answers such a search from the same list it advertises rather than from a
        ///          second one that can drift out of step with it.
        std::vector<dib::block> description_blocks {};
        /// @brief Further description octets, forwarded verbatim after @ref description_blocks.
        /// @details The escape hatch for block types this build does not model. Validated as a well-formed
        ///          run of blocks before being sent, so a malformed one fails here rather than at the peer.
        byte_buffer_t device_info_blocks {};
        /// @brief The MAC address a SEARCH_REQUEST_EXTENDED may select this server by.
        /// @details All-zero means "no MAC configured", and a mandatory select-by-MAC parameter then finds
        ///          no match, so the server stays silent rather than answering a search meant for another.
        mac::storage_t mac_address {};
        /// @brief Whether this server is in programming mode, for select-by-programming-mode searches.
        bool programming_mode {};
        /// @brief The KNX IP Secure configuration; null for a server that is not secure.
        /// @details Set, it secures tunnelling: a client opens a session over TCP before anything else, an unencrypted
        ///          CONNECT_REQUEST is refused with E_CONNECTION_TYPE, and each user is given only its own tunnel
        ///          addresses. The description reports tunnelling in a SECURED_SERVICE_FAMILIES block, unless
        ///          @ref description_blocks carries one already.
        std::shared_ptr<const secure::server_configuration> secure {};
    };

    /// @brief The clocks and the entropy a tunnelling server runs on.
    struct server_options
    {
        /// @brief The monotonic millisecond clock channels time out on; the steady clock when null.
        server_clock_now_function clock_now {};
        /// @brief The clock secure sessions time out on; the steady clock when null.
        secure::monotonic_ms_function secure_clock_ms {};
        /// @brief Where session key pairs come from; @ref kmx::aio::knx::secure::system_entropy when null. Must outlive the
        ///        server.
        secure::entropy_source* entropy {};
    };

    /// @brief A KNXnet/IP tunnelling server: answers discovery, holds channels, yields tunnelled frames.
    class generic_server final
    {
    public:
        /// @brief Creates a server bound to one datagram transport.
        /// @param transport The executor-bound UDP transport to drive.
        /// @param config What this server offers and how it identifies itself.
        /// @param options The clocks channels and secure sessions time out on, and where session key pairs come from.
        /// @throws std::bad_alloc when the secure sessions cannot be allocated.
        explicit generic_server(datagram_transport& transport, server_config config = {}, server_options options = {}) noexcept(false);

        /// @brief Creates a server with no datagram transport, serving connections alone - KNXnet/IP over TCP.
        /// @param config What this server offers and how it identifies itself.
        /// @param options The clocks channels and secure sessions time out on, and where session key pairs come from.
        /// @throws std::bad_alloc when the secure sessions cannot be allocated.
        /// @note @ref serve_once and @ref serve report @ref kmx::aio::knx::error::invalid_configuration: there is no
        ///       datagram transport for them to serve.
        explicit generic_server(server_config config, server_options options = {}) noexcept(false);

        generic_server(const generic_server&) = delete;
        generic_server& operator=(const generic_server&) = delete;
        /// @brief Destroys the server; open channels and sessions are simply abandoned, not disconnected.
        ~generic_server() noexcept;

        /// @brief Handles one incoming datagram.
        /// @return A task yielding the event, or the error that stopped the exchange.
        /// @retval kmx::aio::knx::error::shutdown The server has been shut down.
        /// @retval kmx::aio::knx::error::invalid_length The datagram was empty or larger than the buffer.
        /// @retval kmx::aio::knx::error::timeout Handled discovery request; no server event produced.
        /// @retval kmx::aio::knx::error::secure_frame_required A secure server refused an unencrypted frame.
        /// @details Tunnelled cEMI frames yield a @ref server_event carrying the channel and payload.
        ///          Connection management frames (connect, connectionstate, disconnect) are answered
        ///          and yield a @ref server_event with an empty cEMI payload. Discovery queries are answered
        ///          and report @ref kmx::aio::knx::error::timeout. Channels that have gone quiet are reclaimed
        ///          on the way in, so a caller that stays in this loop needs no separate @ref poll.
        [[nodiscard]] server_event_task_t serve_once() noexcept(false);

        /// @brief Runs @ref serve_once until the server stops or fails.
        /// @return A task yielding nothing, or the error that ended the loop.
        /// @retval kmx::aio::knx::error::shutdown The server was shut down, or the task's stop token was
        ///         signalled.
        /// @note Events are handled and discarded, so this is the loop for a server whose forwarding
        ///       happens elsewhere; a caller that needs the frames drives @ref serve_once itself.
        ///       A timeout is treated as ordinary quiet and does not end the loop.
        [[nodiscard]] task_returning_expected_void_t serve() noexcept(false);

        /// @brief Serves one connection - KNXnet/IP over TCP - until it ends.
        /// @param connection The connection's stream transport, already open.
        /// @param on_event What each frame tunnelled in over @p connection is handed to; frames are dropped when this
        ///        is empty.
        /// @return A task yielding nothing once the peer has closed the connection, or the error that ended it.
        /// @retval kmx::aio::knx::error::shutdown The server was shut down, or the task's stop token was signalled.
        /// @details Requests are answered on @p connection under the rules of KNXnet/IP over TCP: TCP HPAIs, no
        ///          TUNNELLING_ACK, no check of sequence numbers. When the loop ends every channel opened over
        ///          @p connection is released and @p connection is closed. Any number of these may run at once,
        ///          beside @ref serve, on any threads. A secure server also ends the sessions opened on @p connection,
        ///          and closes a connection that has carried no session for
        ///          @ref kmx::aio::knx::secure::server_configuration::unauthenticated_lifetime_ms.
        [[nodiscard]] task_returning_expected_void_t serve_connection(datagram_transport& connection,
                                                                      server_event_handler on_event = {}) noexcept(false);

        /// @brief Sends a cEMI frame to one connected client.
        /// @param channel_id The channel to send on.
        /// @param cemi_bytes The frame to tunnel; must be non-empty.
        /// @return A task yielding nothing, or the error that stopped the send.
        /// @retval kmx::aio::knx::error::invalid_configuration The channel is not open.
        /// @retval kmx::aio::knx::error::malformed_frame @p cemi_bytes is empty.
        /// @retval kmx::aio::knx::error::invalid_length @p cemi_bytes exceeds the largest cEMI message.
        /// @note The frame goes out over the transport the channel was opened on - sealed under its session, for a channel
        ///       opened in one. The server does not wait for a TUNNELLING_ACK; the sequence number is consumed and the
        ///       send reported as soon as the frame is away.
        [[nodiscard]] task_returning_expected_void_t send(std::uint8_t channel_id, cspan_uint8_t cemi_bytes) noexcept(false);

        /// @brief Releases one channel.
        /// @param channel_id The channel to release.
        /// @return Nothing, or @ref kmx::aio::knx::error::invalid_configuration when it was not open.
        /// @note Local only - no DISCONNECT_REQUEST is sent, so the client learns of it when its next
        ///       heartbeat is refused.
        [[nodiscard]] expected_void_t disconnect(std::uint8_t channel_id) noexcept;

        /// @brief Releases every channel and refuses further service.
        /// @return Nothing.
        /// @note Terminal until @ref reset: @ref serve_once and @ref poll report
        ///       @ref kmx::aio::knx::error::shutdown afterwards.
        [[nodiscard]] expected_void_t shutdown() noexcept;

        /// @brief Releases every channel and returns the server to service.
        /// @return Nothing.
        /// @note The way back from @ref shutdown, and how @ref gateway::start brings a server up.
        [[nodiscard]] expected_void_t reset() noexcept;

        /// @brief Reclaims channels that have been silent past the inactivity timeout, and ends secure sessions that have
        ///        timed out, with their channels.
        /// @return Nothing, or @ref kmx::aio::knx::error::shutdown when the server is shut down.
        /// @note Only needed by a caller that is not sitting in @ref serve_once or @ref serve_connection, which do this
        ///       themselves. Channels are not reclaimed when @ref server_config::inactivity_timeout_ms is zero.
        [[nodiscard]] expected_void_t poll() noexcept;

        /// @brief Indicates whether a channel is open.
        /// @param channel_id The channel to test; zero is never open.
        [[nodiscard]] bool channel_active(std::uint8_t channel_id) const noexcept;

        /// @brief Returns how many channels are currently open.
        [[nodiscard]] std::uint8_t active_channels() const noexcept;

        /// @brief Indicates whether this server is secure: built with @ref server_config::secure set.
        [[nodiscard]] bool secured() const noexcept { return sessions_ != nullptr; }

        /// @brief Returns how many secure sessions are open; zero for a server that is not secure.
        [[nodiscard]] std::size_t secure_sessions() const noexcept;

        /// @brief Returns a copy of what the secure sessions have refused and done; all zero for a server that is not secure.
        [[nodiscard]] secure::statistics secure_counters() const noexcept;

    private:
        /// @brief Where a request came from: the transport it arrived on, and the peer that sent it.
        struct origin
        {
            /// @brief The transport to answer on.
            datagram_transport* transport {};
            /// @brief The peer the request came from, or the one to send to.
            transport_peer peer {};
            /// @brief The secure session the request arrived in; zero when it arrived in the clear.
            std::uint16_t session_id {};
        };

        struct channel
        {
            bool active {};
            /// @brief The transport the channel was opened over, which all its traffic keeps using.
            datagram_transport* transport {};
            transport_peer peer {};
            transport_peer data_peer {};
            hpai data_endpoint {};
            individual_address assigned_address {};
            std::uint8_t sequence {};
            bool incoming_sequence_valid {};
            std::uint8_t last_incoming_sequence {};
            std::uint8_t next_incoming_sequence {};
            std::uint32_t last_activity_ms {};
            /// @brief The secure session the channel was opened in; zero for a channel opened in the clear.
            std::uint16_t session_id {};
        };

        /// @brief What offering a channel to a new connection produced.
        /// @details Carried as one value because a refused connection still has to be answered: the
        ///          CONNECT_RESPONSE that names the refusal is built from the same fields as an accepted
        ///          one, and only @ref accepted separates the two.
        struct connection_offer
        {
            /// @brief The allocated channel, or zero when none was free.
            std::uint8_t channel_id {};
            /// @brief The status the CONNECT_RESPONSE reports.
            connect_status status {connect_status::no_more_connections};
            /// @brief The individual address the channel was given.
            individual_address assigned {};
            /// @brief Whether a channel was actually allocated.
            bool accepted {};
        };

        /// @brief What a received TUNNELLING_REQUEST turned out to be.
        enum class indication_verdict : std::uint8_t
        {
            /// @brief Not on a channel this peer holds, or out of order.
            refused,
            /// @brief The request accepted last, sent again.
            repeat,
            /// @brief A request to deliver.
            fresh,
        };

        /// @brief Where a frame the application sends goes, and the sequence number it was given.
        struct outgoing
        {
            /// @brief The channel's transport and data peer.
            origin destination {};
            /// @brief The sequence number taken for the frame.
            std::uint8_t sequence {};
        };

        /// @brief How one frame of a connection went: `true` to read the next, `false` once the connection ended.
        using connection_step_t = std::expected<bool, std::error_code>;

        /// @brief The state of one connection a secure server serves; defined with the secure half of the server.
        struct secure_connection;

        /// @brief Runs one step of bookkeeping on the channel table, holding the table lock.
        /// @details The lock is held for the step alone and never across a suspension, and @p step must not call a
        ///          member that takes it again.
        template <typename Step>
        [[nodiscard]] std::invoke_result_t<Step> with_table(Step&& step) const noexcept(std::is_nothrow_invocable_v<Step>)
        {
            const std::lock_guard lock {table_mutex_};
            return std::forward<Step>(step)();
        }

        /// @brief Indicates whether a failure concerns one request only, leaving a serving loop free to go on.
        [[nodiscard]] static bool recoverable(std::error_code failure) noexcept;
        [[nodiscard]] task_returning_expected_void_t send_datagram(cspan_uint8_t packet, const origin& to) noexcept(false);
        /// @brief Allocates a channel to @p from and records it, taking the table lock.
        [[nodiscard]] connection_offer offer_channel(const origin& from, const std::optional<individual_address>& requested,
                                                     const transport_peer& data_peer, const hpai& data_endpoint) noexcept;
        /// @brief Decodes one received frame and routes it to the handler for its service.
        [[nodiscard]] server_event_task_t handle_received(cspan_uint8_t packet, const origin& from) noexcept(false);
        /// @brief Receives and serves one frame of a connection, handing a tunnelled one to @p on_event.
        [[nodiscard]] task<connection_step_t> serve_connection_frame(datagram_transport& connection, span_uint8_t buffer,
                                                                     const server_event_handler& on_event) noexcept(false);
        /// @brief Routes one decoded datagram to the handler for its service.
        [[nodiscard]] server_event_task_t dispatch(const datagram& value, const origin& from) noexcept(false);
        [[nodiscard]] server_event_task_t open_channel(const connect_request_frame& request, std::uint16_t service_type,
                                                       const origin& from) noexcept(false);
        [[nodiscard]] server_event_task_t open_ipv6_channel(const ipv6_connect_request_frame& request, std::uint16_t service_type,
                                                            const origin& from) noexcept(false);
        /// @brief Sends the CONNECT_RESPONSE for an offer, then reports the new channel or the refusal.
        [[nodiscard]] server_event_task_t answer_connect(const connection_offer& offer, const hpai& data_endpoint,
                                                         const origin& from) noexcept(false);
        [[nodiscard]] server_event_task_t answer_connectionstate(const connectionstate_request_frame& request,
                                                                 const origin& from) noexcept(false);
        [[nodiscard]] server_event_task_t answer_disconnect(const disconnect_request_frame& request, const origin& from) noexcept(false);
        /// @brief Sends the CONNECT_RESPONSE refusing a request, then reports @p reason.
        [[nodiscard]] server_event_task_t refuse_connect(const hpai& data_endpoint, connect_status status, std::error_code reason,
                                                         const origin& from) noexcept(false);
        /// @brief Sends the IPv6 CONNECT_RESPONSE refusing a request, then reports @p reason.
        [[nodiscard]] server_event_task_t refuse_ipv6_connect(const ipv6_hpai& data_endpoint, connect_status status, std::error_code reason,
                                                              const origin& from) noexcept(false);
        /// @brief Answers a CONNECTIONSTATE or DISCONNECT request naming a channel this peer does not hold.
        [[nodiscard]] server_event_task_t refuse_unknown_channel(std::uint16_t response_service, std::uint8_t channel_id,
                                                                 const origin& from) noexcept(false);
        [[nodiscard]] server_event_task_t accept_tunnelled(const tunnelling_request_frame& request, const origin& from) noexcept(false);
        /// @brief Decides what a TUNNELLING_REQUEST is and records it; the table lock must be held.
        [[nodiscard]] indication_verdict classify_indication(const tunnelling_request_frame& request, const origin& from) noexcept;
        [[nodiscard]] task_returning_expected_void_t send_tunnelling_ack(std::uint8_t channel_id, std::uint8_t sequence_number,
                                                                         const origin& to) noexcept(false);
        [[nodiscard]] bool answers_discovery() const noexcept;
        [[nodiscard]] server_event_task_t answer_search(const discovery::search_request_frame& request, const origin& from) noexcept(false);
        [[nodiscard]] server_event_task_t answer_extended_search(const discovery::extended_search_request_frame& request,
                                                                 const origin& from) noexcept(false);
        [[nodiscard]] bool matches_search_parameters(const discovery::extended_search_request_frame& request) const noexcept;
        [[nodiscard]] bool serves_service_family(const dib::service_family family, std::uint8_t version) const noexcept;
        [[nodiscard]] expected_void_t build_discovery_packets() noexcept(false);
        /// @brief Returns the description blocks advertised: the configured ones, and for a secure server its secured
        ///        families.
        [[nodiscard]] std::vector<dib::block> advertised_blocks() const noexcept(false);
        [[nodiscard]] byte_buffer_t requested_description_blocks(const discovery::extended_search_request_frame& request) const;
        [[nodiscard]] server_event_task_t answer_description(const discovery::description_request_frame& request,
                                                             const origin& from) noexcept(false);
        [[nodiscard]] bool peer_matches(const channel& value, const origin& from, bool data_endpoint = false) const noexcept;
        /// @brief Indicates whether @p from holds an open channel; the table lock must be held.
        [[nodiscard]] bool holds_channel(std::uint8_t channel_id, const origin& from, bool data_endpoint = false) const noexcept;
        /// @brief Indicates whether a channel is open; the table lock must be held.
        [[nodiscard]] bool channel_open(std::uint8_t channel_id) const noexcept;
        /// @brief Picks a free channel, or the one carrying a requested address; the table lock must be held.
        [[nodiscard]] std::expected<std::uint8_t, connect_status> allocate_channel(
            const std::optional<individual_address>& requested) const noexcept;
        [[nodiscard]] individual_address assigned_address(std::uint8_t channel_id) const noexcept;
        [[nodiscard]] std::uint32_t now_ms() const noexcept;
        /// @brief Records traffic on a channel; the table lock must be held.
        void observe_activity(channel& value) noexcept;
        /// @brief Records traffic on a channel that is still open, taking the table lock.
        void note_activity(std::uint8_t channel_id) noexcept;
        /// @brief Takes the next sequence number of an open channel, with where to send on it, taking the table lock.
        [[nodiscard]] std::optional<outgoing> claim_sequence(std::uint8_t channel_id) noexcept;
        /// @brief Releases one channel; the table lock must be held.
        void release_channel(std::uint8_t channel_id) noexcept;
        /// @brief Releases every channel opened over @p connection; the table lock must be held.
        void release_channels_of(const datagram_transport& connection) noexcept;
        /// @brief Releases every channel; the table lock must be held.
        void release_all_channels() noexcept;

        /// @brief Indicates whether a secure server refuses a request for arriving in the clear, counting it when it does.
        [[nodiscard]] bool refuses_in_clear(const origin& from) const noexcept;
        [[nodiscard]] std::uint64_t secure_now_ms() const noexcept;
        /// @brief Serves one connection of a secure server until it ends.
        [[nodiscard]] task_returning_expected_void_t serve_secure_connection(datagram_transport& connection,
                                                                             const server_event_handler& on_event) noexcept(false);
        /// @brief Receives and serves one frame of a secure connection.
        [[nodiscard]] task<connection_step_t> serve_secure_frame(secure_connection& state,
                                                                 const server_event_handler& on_event) noexcept(false);
        /// @brief Routes one frame of a secure connection: a SESSION_REQUEST, a wrapper, or a frame sent in the clear.
        [[nodiscard]] task<connection_step_t> route_secure_frame(secure_connection& state, cspan_uint8_t packet, const transport_peer& peer,
                                                                 const server_event_handler& on_event) noexcept(false);
        /// @brief Opens a session on a connection and sends its SESSION_RESPONSE.
        [[nodiscard]] task<connection_step_t> answer_session_request(secure_connection& state, const secure::session_request_frame& request,
                                                                     const transport_peer& peer) noexcept(false);
        /// @brief Tells a client, in the clear, that no session was opened for its SESSION_REQUEST.
        [[nodiscard]] task<connection_step_t> refuse_session(datagram_transport& connection, const transport_peer& peer) noexcept(false);
        /// @brief Opens a wrapper and acts on what it carried.
        [[nodiscard]] task<connection_step_t> accept_wrapper(secure_connection& state, const secure::wrapper_frame& wrapper,
                                                             const transport_peer& peer,
                                                             const server_event_handler& on_event) noexcept(false);
        /// @brief Serves a frame a session carried, handing a tunnelled one to @p on_event.
        [[nodiscard]] task<connection_step_t> serve_session_frame(secure::session_link& link, cspan_uint8_t packet,
                                                                  const transport_peer& peer,
                                                                  const server_event_handler& on_event) noexcept(false);
        /// @brief Answers a SESSION_AUTHENTICATE that did not verify, then ends its session.
        [[nodiscard]] task<connection_step_t> refuse_authentication(secure::session_link& link) noexcept(false);
        /// @brief Indicates whether a secure connection has carried no session for as long as a handshake may take.
        [[nodiscard]] bool connection_idle(secure_connection& state) const noexcept;
        /// @brief Releases the channels of a secure connection's sessions, then ends the sessions.
        void end_secure_connection(secure_connection& state) noexcept;
        /// @brief Allocates a channel in a secure session, under one of its user's tunnel addresses, taking the table lock.
        [[nodiscard]] connection_offer offer_secure_channel(const origin& from, const std::optional<individual_address>& requested,
                                                            const transport_peer& data_peer, const hpai& data_endpoint) noexcept;
        /// @brief Picks the tunnel address a secure channel is given; the table lock must be held.
        [[nodiscard]] std::expected<individual_address, connect_status> permitted_address(
            std::span<const individual_address> permitted, const std::optional<individual_address>& requested) const noexcept;
        /// @brief Releases every channel whose secure session has ended; the table lock must be held.
        void release_orphaned_channels() noexcept;

        datagram_transport* transport_ {};
        server_config config_ {};
        server_clock_now_function clock_now_ {};
        /// @brief Guards @ref channels_ and the building of the discovery answers; see @ref with_table.
        mutable std::mutex table_mutex_ {};
        std::array<channel, 256u> channels_ {};
        std::array<std::uint8_t, frame::max_datagram_size> receive_buffer_ {};
        /// @brief The fixed discovery answers, encoded once by @ref build_discovery_packets.
        /// @details Every octet of them comes from @ref config_, which nothing changes after construction,
        ///          and no field of a request varies them - a SEARCH_REQUEST_EXTENDED's parameters only
        ///          decide whether this server answers, never what it answers with. Encoding them per
        ///          request would redo that work once for every device that hears a multicast search.
        byte_buffer_t search_response_packet_ {};
        byte_buffer_t description_response_packet_ {};
        byte_buffer_t description_blocks_ {};
        bool discovery_packets_built_ {};
        std::atomic_bool shutdown_ {};
        secure::monotonic_ms_function secure_clock_ms_ {};
        /// @brief The secure sessions, which lock themselves; null for a server that is not secure.
        /// @details Taken after @ref table_mutex_ when both are held, never before.
        std::unique_ptr<secure::server_session_table> sessions_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
