/// @file aio/knx/server.hpp
/// @brief Executor-neutral KNXnet/IP tunnelling server.
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
/// @reference KNX System Specifications, 03/08/02 "Core" and 03/08/04 "Tunnelling".
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <algorithm>
        #include <array>
        #include <cstdint>
        #include <expected>
        #include <span>
        #include <sys/socket.h>
        #include <vector>
    #endif

    #include <kmx/aio/mac.hpp>
    #include <kmx/aio/task.hpp>
    #include <kmx/aio/knx/connection.hpp>
    #include <kmx/aio/knx/datagram.hpp>
    #include <kmx/aio/knx/dib.hpp>
    #include <kmx/aio/knx/discovery.hpp>
    #include <kmx/aio/knx/transport.hpp>

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
        ///          out. The range starting here must be one no real device on the installation occupies.
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
    };

    /// @brief One cEMI frame a client tunnelled in, and the channel it arrived on.
    /// @note The channel matters as much as the frame: it is what an answer is sent back on, and what says
    ///       which client's assigned address the frame was sent under.
    struct server_event
    {
        /// @brief The channel the frame arrived on.
        std::uint8_t channel_id {};
        /// @brief The cEMI frame, copied out of the receive buffer.
        byte_buffer_t cemi_bytes {};
    };

    /// @brief A server event, or the error explaining why none was obtained.
    using server_event_result_t = std::expected<server_event, std::error_code>;
    /// @brief Task yielding a server event or the error that stopped the exchange.
    using server_event_task_t = task<server_event_result_t>;

    /// @brief A KNXnet/IP tunnelling server: answers discovery, holds channels, yields tunnelled frames.
    class generic_server final
    {
    public:
        /// @brief Creates a server bound to one transport.
        /// @param transport The executor-bound UDP transport to drive.
        /// @param config What this server offers and how it identifies itself.
        /// @param clock_now The monotonic millisecond clock; the steady clock when null.
        generic_server(datagram_transport& transport, server_config config = {},
                   server_clock_now_function clock_now = nullptr) noexcept;
        generic_server(const generic_server&) = delete;
        generic_server& operator=(const generic_server&) = delete;
        /// @brief Destroys the server; open channels are simply abandoned, not disconnected.
        ~generic_server() noexcept = default;

        /// @brief Handles one incoming datagram.
        /// @return A task yielding the event, or the error that stopped the exchange.
        /// @retval kmx::aio::knx::error::shutdown The server has been shut down.
        /// @retval kmx::aio::knx::error::invalid_length The datagram was empty or larger than the buffer.
        /// @retval kmx::aio::knx::error::timeout Handled discovery request; no server event produced.
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

        /// @brief Sends a cEMI frame to one connected client.
        /// @param channel_id The channel to send on.
        /// @param cemi_bytes The frame to tunnel; must be non-empty.
        /// @return A task yielding nothing, or the error that stopped the send.
        /// @retval kmx::aio::knx::error::invalid_configuration The channel is not open.
        /// @retval kmx::aio::knx::error::malformed_frame @p cemi_bytes is empty.
        /// @retval kmx::aio::knx::error::invalid_length @p cemi_bytes exceeds the largest cEMI message.
        /// @note The server does not wait for the TUNNELLING_ACK; the sequence number is consumed and the
        ///       send reported as soon as the datagram is away.
        [[nodiscard]] task_returning_expected_void_t send(
            std::uint8_t channel_id, cspan_uint8_t cemi_bytes) noexcept(false);

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

        /// @brief Reclaims channels that have been silent past the inactivity timeout.
        /// @return Nothing, or @ref kmx::aio::knx::error::shutdown when the server is shut down.
        /// @note Only needed by a caller that is not sitting in @ref serve_once, which does this itself.
        ///       Does nothing when @ref server_config::inactivity_timeout_ms is zero.
        [[nodiscard]] expected_void_t poll() noexcept;

        /// @brief Indicates whether a channel is open.
        /// @param channel_id The channel to test; zero is never open.
        [[nodiscard]] bool channel_active(std::uint8_t channel_id) const noexcept;

        /// @brief Returns how many channels are currently open.
        [[nodiscard]] std::uint8_t active_channels() const noexcept;

    private:
        struct channel
        {
            bool active {};
            transport_peer peer {};
            transport_peer data_peer {};
            hpai data_endpoint {};
            individual_address assigned_address {};
            std::uint8_t sequence {};
            bool incoming_sequence_valid {};
            std::uint8_t last_incoming_sequence {};
            std::uint8_t next_incoming_sequence {};
            std::uint32_t last_activity_ms {};
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

        [[nodiscard]] task_returning_expected_void_t send_datagram(
            cspan_uint8_t packet, const transport_peer& peer) noexcept(false);
        [[nodiscard]] connection_offer offer_channel(const transport_peer& peer) noexcept;
        /// @brief Routes one decoded datagram to the handler for its service.
        [[nodiscard]] server_event_task_t dispatch(const datagram& value, const transport_peer& peer) noexcept(false);
        [[nodiscard]] server_event_task_t open_channel(const connect_request_frame& request, std::uint16_t service_type,
                                                       const transport_peer& peer) noexcept(false);
        [[nodiscard]] server_event_task_t open_ipv6_channel(const ipv6_connect_request_frame& request,
                                                            std::uint16_t service_type,
                                                            const transport_peer& peer) noexcept(false);
        [[nodiscard]] server_event_task_t answer_connectionstate(const connectionstate_request_frame& request,
                                                                 const transport_peer& peer) noexcept(false);
        [[nodiscard]] server_event_task_t answer_disconnect(const disconnect_request_frame& request,
                                                            const transport_peer& peer) noexcept(false);
        [[nodiscard]] server_event_task_t accept_tunnelled(const tunnelling_request_frame& request,
                                                           const transport_peer& peer) noexcept(false);
        [[nodiscard]] task_returning_expected_void_t send_tunnelling_ack(std::uint8_t channel_id, std::uint8_t sequence_number,
                                                                        const transport_peer& peer) noexcept(false);
        [[nodiscard]] bool answers_discovery() const noexcept;
        [[nodiscard]] server_event_task_t answer_search(
            const discovery::search_request_frame& request, const transport_peer& source) noexcept(false);
        [[nodiscard]] server_event_task_t answer_extended_search(
            const discovery::extended_search_request_frame& request, const transport_peer& source) noexcept(false);
        [[nodiscard]] bool matches_search_parameters(const discovery::extended_search_request_frame& request) const noexcept;
        [[nodiscard]] bool serves_service_family(const dib::service_family family, std::uint8_t version) const noexcept;
        [[nodiscard]] expected_void_t build_discovery_packets() noexcept(false);
        [[nodiscard]] byte_buffer_t requested_description_blocks(
            const discovery::extended_search_request_frame& request) const;
        [[nodiscard]] server_event_task_t answer_description(
            const discovery::description_request_frame& request, const transport_peer& source) noexcept(false);
        [[nodiscard]] bool peer_matches(const channel& value, const transport_peer& peer,
                        bool data_endpoint = false) const noexcept;
        [[nodiscard]] std::expected<std::uint8_t, std::error_code> allocate_channel() noexcept;
        [[nodiscard]] individual_address assigned_address(std::uint8_t channel_id) const noexcept;
        [[nodiscard]] std::uint32_t now_ms() const noexcept;
        void observe_activity(channel& value) noexcept;
        void release_channel(std::uint8_t channel_id) noexcept;

        datagram_transport& transport_;
        server_config config_ {};
        server_clock_now_function clock_now_ {};
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
        bool shutdown_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
