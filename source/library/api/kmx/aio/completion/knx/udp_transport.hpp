/// @file aio/completion/knx/udp_transport.hpp
/// @brief Completion UDP adapter for the executor-neutral KNX transport contract.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_COMPLETION) && defined(KMX_AIO_FEATURE_KNX)

    #include <chrono>
    #include <cerrno>
    #include <cstring>
    #include <netinet/in.h>
    #include <kmx/aio/knx/transport.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/routing.hpp>
    #include <kmx/aio/completion/udp/endpoint.hpp>

namespace kmx::aio::completion::knx
{
    class udp_transport final: public kmx::aio::knx::datagram_transport
    {
    public:
        explicit udp_transport(udp::endpoint& endpoint) noexcept: endpoint_(endpoint) {}

        [[nodiscard]] task_returning_expected_size_t send(
            const cspan_byte_t payload, const sockaddr* peer, const ::socklen_t peer_length) noexcept(false) override
        {
            co_return co_await endpoint_.send(payload, peer, peer_length);
        }

        [[nodiscard]] task_returning_expected_size_t receive(
            const span_byte_t buffer, kmx::aio::knx::transport_peer& peer) noexcept(false) override
        {
            co_return co_await endpoint_.recv(buffer, peer.address, peer.length);
        }

        [[nodiscard]] task_returning_expected_size_t receive_until(
            const span_byte_t buffer, kmx::aio::knx::transport_peer& peer,
            const std::uint32_t deadline_ms) noexcept(false) override;

        [[nodiscard]] expected_void_t join_multicast_group(
            const kmx::aio::knx::multicast_group_configuration& configuration) noexcept override;

        [[nodiscard]] expected_void_t leave_multicast_group(
            const kmx::aio::knx::multicast_group_configuration& configuration) noexcept override;

    private:
        [[nodiscard]] static in_addr make_multicast_address(
            const std::array<std::uint8_t, 4u>& group) noexcept;

        udp::endpoint& endpoint_;
    };
}
#endif // KMX_AIO_FEATURE_COMPLETION && KMX_AIO_FEATURE_KNX
