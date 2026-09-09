/// @file aio/knx/transport.hpp
/// @brief Executor-neutral UDP transport contract for KNXnet/IP sessions.
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
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <array>
        #include <cstddef>
        #include <cstdint>
        #include <cstring>
        #include <span>
        #include <sys/socket.h>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/task.hpp>

namespace kmx::aio::knx
{
    /// @brief Non-owning peer address returned by a datagram transport.
    /// @details Storage large enough for any address family, plus the length actually written into it.
    ///          The two travel together because a `sockaddr_storage` says nothing about how much of itself
    ///          is meaningful, and code that guesses from the family reads octets the transport never set.
    struct transport_peer
    {
        /// @brief The peer address, valid in its first @ref length octets.
        sockaddr_storage address {};
        /// @brief How many octets of @ref address the transport filled in.
        ::socklen_t length = 0u;
    };

    /// @brief Copies a socket address of a caller-declared length into owned `sockaddr_storage`.
    /// @param destination The storage to fill; zeroed first, so the octets past @p length are defined.
    /// @param source The address to copy; may be smaller than `sockaddr_storage`.
    /// @param length The number of octets @p source actually provides.
    /// @return `false` when @p source is null or @p length does not fit `sockaddr_storage`.
    /// @details Only @p length octets are read. A caller holding a `sockaddr_in` has 16 octets, not the
    ///          128 of a `sockaddr_storage`, and copying the larger type out of the smaller object reads
    ///          past it - which is why the size to copy is taken from the length argument and never from
    ///          the destination type.
    [[nodiscard]] inline bool store_socket_address(sockaddr_storage& destination, const sockaddr* const source,
                                                   const ::socklen_t length) noexcept
    {
        destination = sockaddr_storage {};
        if ((source == nullptr) || (length <= 0) || (static_cast<std::size_t>(length) > sizeof(sockaddr_storage)))
            return false;

        std::memcpy(&destination, source, static_cast<std::size_t>(length));
        return true;
    }

    /// @brief The multicast group a routing endpoint joins, and the interface it joins it on.
    /// @details The defaults are the KNXnet/IP system setup: 224.0.23.12 on port 3671. A KNX installation
    ///          that segments its routing traffic overrides the group; a multi-homed host has to name the
    ///          interface as well, because the kernel's default route is rarely the one the bus is on.
    /// @reference KNX System Specifications, 03/08/02 "Core", routing multicast address.
    struct multicast_group_configuration
    {
        /// @brief The multicast group address; must be in 224.0.0.0/4.
        ipv4::storage_t group {224u, 0u, 23u, 12u};
        /// @brief The UDP port to join on.
        std::uint16_t port = 3671u;
        /// @brief The index of the interface to join on; zero lets the kernel choose.
        std::uint32_t interface_index {};
    };

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
        [[nodiscard]] virtual task_returning_expected_size_t send(
            cspan_byte_t payload, const sockaddr* peer, ::socklen_t peer_length) noexcept(false) = 0;

        /// @brief Waits for one datagram, with no deadline of its own.
        /// @param buffer The storage to receive into; a datagram longer than this may be truncated.
        /// @param peer Filled in with the sender's address and its length.
        /// @return A task yielding the number of octets received, or the error that stopped the receive.
        [[nodiscard]] virtual task_returning_expected_size_t receive(
            span_byte_t buffer, transport_peer& peer) noexcept(false) = 0;

        /// @brief Waits for one datagram, giving up at a deadline.
        /// @param buffer The storage to receive into.
        /// @param peer Filled in with the sender's address and its length.
        /// @return A task yielding the number of octets received, or the error that stopped the receive.
        /// @details The deadline is a monotonic millisecond stamp on the same clock the KNX layer reads,
        ///          not a duration. This default ignores it and waits indefinitely, which is correct only
        ///          for a transport whose endpoint already carries a timeout: every retry the session layer
        ///          drives depends on this returning, so an implementation that can honour the deadline
        ///          @b must override this rather than inherit the wait.
        [[nodiscard]] virtual task_returning_expected_size_t receive_until(
            span_byte_t buffer, transport_peer& peer, const std::uint32_t) noexcept(false)
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
    };
}
#endif // KMX_AIO_FEATURE_KNX
