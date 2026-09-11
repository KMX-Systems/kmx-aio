/// @file api/kmx/aio/knx/discovery/client.hpp
/// @brief KNXnet/IP discovery client: searches for interfaces and asks one of them to describe itself.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// A SEARCH is a multicast question answered by every server that hears it, and a DESCRIPTION the unicast
/// follow-up to one of them. That difference in shape is why @ref kmx::aio::knx::discovery::client offers both
/// @ref kmx::aio::knx::discovery::client::search and
/// @ref kmx::aio::knx::discovery::client::search_all. A search has no single answer, so the honest
/// operation is the one bounded by a collection window; taking the first response that arrives is only
/// meaningful when the peer is a single known server.
/// @reference KNX System Specifications, 03/08/02 "Core", discovery.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/datagram_transport.hpp>
        #include <kmx/aio/knx/discovery.hpp>
        #include <kmx/aio/knx/frame.hpp>
        #include <kmx/aio/knx/transport.hpp>
        #include <kmx/aio/task.hpp>

        #include <array>
        #include <cstdint>
        #include <vector>
        #include <sys/socket.h>
    #endif

namespace kmx::aio::knx::discovery
{
    /// @brief Monotonic millisecond clock, for tests that need a deterministic one.
    using clock_now_function = std::uint32_t (*)() noexcept;

    /// @brief Timing policy for discovery.
    struct client_config
    {
        /// @brief How long @ref kmx::aio::knx::discovery::client::search_all collects responses.
        /// @details A search is answered by every server that hears it, so the operation is bounded by a
        ///          window rather than by the first answer.
        std::uint32_t search_timeout_ms = 3'000u;
        /// @brief How long to wait for a single unicast answer, as a DESCRIPTION_RESPONSE is.
        std::uint32_t response_timeout_ms = 10'000u;
    };

    /// @brief Sends discovery and description requests over one transport, and collects the answers.
    class client final
    {
    public:
        /// @brief Creates a discovery client aimed at one endpoint.
        /// @param transport The executor-bound UDP transport to drive.
        /// @param peer The endpoint to search; only @p peer_length octets are read.
        /// @param peer_length The number of octets @p peer provides - `sizeof(sockaddr_in)` for IPv4.
        /// @note This is the overload to use when holding a `sockaddr_in` or `sockaddr_in6`. Casting one
        ///       to `sockaddr_storage&` to reach the overload below reads past the object.
        client(datagram_transport& transport, const sockaddr* const peer, const ::socklen_t peer_length) noexcept: transport_(transport)
        {
            // In the body rather than the member initialiser list: storing into peer_ from peer_length_'s
            // initialiser happens to work only because peer_ is declared first, and would silently write an
            // uninitialised object if the members were ever reordered.
            if (store_socket_address(peer_, peer, peer_length))
            {
                peer_length_ = peer_length;
                // Decided once here rather than per datagram: peer_ is never written again, and
                // peer_matches() runs on every answer a search collects.
                peer_is_multicast_ = address_is_multicast();
            }
        }

        /// @brief Creates a discovery client from a peer the caller already holds as `sockaddr_storage`.
        /// @copydetails client
        client(datagram_transport& transport, const sockaddr_storage& peer, const ::socklen_t peer_length) noexcept:
            client(transport, reinterpret_cast<const sockaddr*>(&peer), peer_length)
        {
        }

        /// @brief Sets the timing policy.
        void configure(const client_config value) noexcept { config_ = value; }
        /// @brief Sets the monotonic millisecond clock; the steady clock is used when null.
        void set_clock(const clock_now_function value) noexcept { clock_now_ = value; }

        /// @brief Sends one SEARCH_REQUEST and returns the first answer.
        /// @note Answers only the point-to-point case well. When the configured peer is the discovery
        ///       multicast group, prefer @ref search_all: several servers answer, and which one arrives
        ///       first is not a property a caller should depend on.
        [[nodiscard]] search_task_t search(const search_request_frame& request) noexcept(false);
        /// @brief Sends one SEARCH_REQUEST over IPv6 and returns the first answer.
        /// @param request The request, whose discovery endpoint names where answers are to be sent.
        /// @return A task yielding the first response, or the error that stopped the search.
        [[nodiscard]] search_task_t search(const ipv6_search_request_frame& request) noexcept(false);

        /// @brief Sends one SEARCH_REQUEST and collects every answer within the configured window.
        /// @param request The request, whose discovery endpoint names where answers are to be sent.
        /// @return Every server that answered, in arrival order; empty when none did.
        [[nodiscard]] search_all_task_t search_all(const search_request_frame& request) noexcept(false);
        /// @brief Sends one SEARCH_REQUEST over IPv6 and collects every answer within the window.
        /// @param request The request, whose discovery endpoint names where answers are to be sent.
        /// @return Every server that answered, in arrival order; empty when none did.
        [[nodiscard]] search_all_task_t search_all(const ipv6_search_request_frame& request) noexcept(false);

        /// @brief Sends a SEARCH_REQUEST_EXTENDED and collects every answer within the configured window.
        /// @param request The request, including the parameters narrowing which servers should answer.
        /// @return Every server that answered, in arrival order; empty when none did.
        /// @note A server that predates the extended service ignores it, so a client that wants to find
        ///       everything searches with both this and @ref search_all.
        [[nodiscard]] search_all_task_t search_all(const extended_search_request_frame& request) noexcept(false);

        /// @brief Sends a DESCRIPTION_REQUEST to the configured peer and returns its description.
        /// @param request The request, whose control endpoint names where the answer is to be sent.
        /// @return The device information blocks the server reported.
        [[nodiscard]] description_task_t describe(const description_request_frame& request) noexcept(false);

    private:
        [[nodiscard]] search_task_t search_packet(cspan_uint8_t packet) noexcept(false);
        [[nodiscard]] search_all_task_t search_all_packet(cspan_uint8_t packet) noexcept(false);
        [[nodiscard]] task_returning_expected_void_t send_packet(cspan_uint8_t packet) noexcept(false);
        /// @brief Decodes one search answer and adds it to @p found, ignoring anything unreadable.
        static void collect_response(cspan_uint8_t datagram, const transport_peer& peer, std::vector<discovered_server>& found) noexcept;
        [[nodiscard]] bool peer_matches(const transport_peer& peer) const noexcept;
        [[nodiscard]] bool address_is_multicast() const noexcept;
        [[nodiscard]] std::uint32_t now_ms() const noexcept;

        datagram_transport& transport_;
        sockaddr_storage peer_ {};
        ::socklen_t peer_length_ = 0u;
        bool peer_is_multicast_ {};
        client_config config_ {};
        clock_now_function clock_now_ {};
        std::array<std::uint8_t, frame::max_datagram_size> buffer_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
