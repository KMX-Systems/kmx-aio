/// @file api/kmx/aio/knx/datagram_transport.hpp
/// @brief Executor-neutral UDP transport contract for KNXnet/IP sessions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// Everything above this header - discovery, tunnelling, routing - is written against
/// @ref kmx::aio::knx::datagram_transport and knows nothing about how the datagrams are actually moved.
/// That is what lets one KNX implementation serve both I/O models the library ships: a completion adapter
/// and a readiness adapter each implement this contract, and the protocol code is compiled once rather
/// than once per executor.
///
/// The split of responsibility is deliberate and narrow. An implementation owns the socket, the executor
/// it is bound to, and nothing else; packet framing, retransmission, sequence counters and state
/// transitions all stay in the KNX layer, so a new transport cannot get the protocol wrong.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/transport.hpp>
        #include <kmx/aio/task.hpp>

        #include <cstdint>
        #include <expected>
        #include <system_error>
        #include <sys/socket.h>
    #endif

namespace kmx::aio::knx
{
    /// @brief Socket-independent asynchronous UDP contract used by the KNX session layer.
    /// @details Implementations own the executor-bound UDP endpoint and provide the actual
    /// readiness or completion I/O. KNX code owns packet framing, retries, and state transitions.
    class datagram_transport
    {
    public:
        /// @brief Constructs a transport that owns no endpoint yet.
        datagram_transport() noexcept = default;
        datagram_transport(const datagram_transport&) = delete;
        datagram_transport& operator=(const datagram_transport&) = delete;
        /// @brief Destroys the transport; the derived endpoint is released with it.
        virtual ~datagram_transport() noexcept = default;

        /// @brief Sends one datagram to a peer.
        /// @param payload The octets to send, as one datagram.
        /// @param peer The destination address; only @p peer_length octets are read.
        /// @param peer_length The number of octets @p peer provides - `sizeof(sockaddr_in)` for IPv4.
        /// @return A task yielding the number of octets sent, or the error that stopped the send.
        [[nodiscard]] virtual task_returning_expected_size_t send(cspan_byte_t payload, const sockaddr* peer,
                                                                  ::socklen_t peer_length) noexcept(false) = 0;

        /// @brief Waits for one datagram, with no deadline of its own.
        /// @param buffer The storage to receive into; a datagram longer than this may be truncated.
        /// @param peer Filled in with the sender's address and its length.
        /// @return A task yielding the number of octets received, or the error that stopped the receive.
        [[nodiscard]] virtual task_returning_expected_size_t receive(span_byte_t buffer, transport_peer& peer) noexcept(false) = 0;

        /// @brief Waits for one datagram, giving up at a deadline.
        /// @param buffer The storage to receive into.
        /// @param peer Filled in with the sender's address and its length.
        /// @return A task yielding the number of octets received, or the error that stopped the receive.
        /// @details The deadline is a monotonic millisecond stamp on the same clock the KNX layer reads,
        ///          not a duration. This default ignores it and waits indefinitely, which is correct only
        ///          for a transport whose endpoint already carries a timeout: every retry the session layer
        ///          drives depends on this returning, so an implementation that can honour the deadline
        ///          @b must override this rather than inherit the wait.
        [[nodiscard]] virtual task_returning_expected_size_t receive_until(span_byte_t buffer, transport_peer& peer,
                                                                           const std::uint32_t) noexcept(false)
        {
            co_return co_await receive(buffer, peer);
        }

        /// @brief Joins the routing multicast group.
        /// @return Nothing, or the reason the group could not be joined.
        /// @note The default reports `std::errc::operation_not_supported`, which is what a unicast-only
        ///       transport should do: routing then fails at start rather than silently receiving nothing.
        [[nodiscard]] virtual expected_void_t join_multicast_group(const multicast_group_configuration&) noexcept
        {
            return std::unexpected(std::make_error_code(std::errc::operation_not_supported));
        }

        /// @brief Leaves the routing multicast group.
        /// @return Nothing, or the reason the group could not be left.
        /// @note As with @ref join_multicast_group, the default reports that the operation is unsupported.
        [[nodiscard]] virtual expected_void_t leave_multicast_group(const multicast_group_configuration&) noexcept
        {
            return std::unexpected(std::make_error_code(std::errc::operation_not_supported));
        }

        /// @brief Opens the connection a stream transport runs over.
        /// @return A task yielding nothing, or the reason the connection could not be opened.
        /// @note A datagram transport has nothing to open, and this default succeeds.
        [[nodiscard]] virtual task_returning_expected_void_t open() noexcept(false) { co_return expected_void_t {}; }

        /// @brief Closes the connection a stream transport runs over; any wait on it ends.
        /// @note A datagram transport has nothing to close, and this default does nothing.
        virtual void close() noexcept {}

        /// @brief Indicates whether frames travel on a byte stream rather than one per datagram.
        /// @details A stream transport - KNXnet/IP over TCP - selects the TCP rules of KNXnet/IP: TCP HPAIs, no
        ///          TUNNELLING_ACK sent or awaited, and a heartbeat kept.
        [[nodiscard]] virtual bool stream_oriented() const noexcept { return false; }
    };
}
#endif // KMX_AIO_FEATURE_KNX
