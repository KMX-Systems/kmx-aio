/// @file aio/knx/transport.hpp
/// @brief Executor-neutral UDP transport contract for KNXnet/IP sessions.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <array>
        #include <cstddef>
        #include <cstdint>
        #include <span>
        #include <sys/socket.h>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/task.hpp>

namespace kmx::aio::knx
{
    /// @brief Non-owning peer address returned by a datagram transport.
    struct transport_peer
    {
        sockaddr_storage address {};
        ::socklen_t length = 0u;
    };

    struct multicast_group_configuration
    {
        std::array<std::uint8_t, 4u> group {224u, 0u, 23u, 12u};
        std::uint16_t port = 3671u;
        std::uint32_t interface_index {};
    };

    /// @brief Socket-independent asynchronous UDP contract used by the KNX session layer.
    /// @details Implementations own the executor-bound UDP endpoint and provide the actual
    /// readiness or completion I/O. KNX code owns packet framing, retries, and state transitions.
    class datagram_transport
    {
    public:
        datagram_transport() noexcept = default;
        datagram_transport(const datagram_transport&) = delete;
        datagram_transport& operator=(const datagram_transport&) = delete;
        virtual ~datagram_transport() noexcept = default;

        [[nodiscard]] virtual task_returning_expected_size_t send(
            cspan_byte_t payload, const sockaddr* peer, ::socklen_t peer_length) noexcept(false) = 0;

        [[nodiscard]] virtual task_returning_expected_size_t receive(
            span_byte_t buffer, transport_peer& peer) noexcept(false) = 0;

        [[nodiscard]] virtual task_returning_expected_size_t receive_until(
            span_byte_t buffer, transport_peer& peer, const std::uint32_t) noexcept(false)
        {
            co_return co_await receive(buffer, peer);
        }

        [[nodiscard]] virtual expected_void_t join_multicast_group(const multicast_group_configuration&) noexcept
        {
            return std::unexpected(std::make_error_code(std::errc::operation_not_supported));
        }

        [[nodiscard]] virtual expected_void_t leave_multicast_group(const multicast_group_configuration&) noexcept
        {
            return std::unexpected(std::make_error_code(std::errc::operation_not_supported));
        }
    };
}
#endif // KMX_AIO_FEATURE_KNX
