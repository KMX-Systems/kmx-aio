/// @file aio/knx/server.hpp
/// @brief Executor-neutral KNXnet/IP tunnelling server.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <array>
    #include <cstdint>
    #include <expected>
    #include <span>
    #include <sys/socket.h>
    #include <vector>
#endif

#include <kmx/aio/task.hpp>
#include <kmx/aio/knx/connection.hpp>
#include <kmx/aio/knx/datagram.hpp>
#include <kmx/aio/knx/transport.hpp>

namespace kmx::aio::knx
{
    using server_clock_now_function = std::uint32_t (*)() noexcept;

    struct server_config
    {
        std::uint8_t max_channels = 16u;
        individual_address first_assigned_address {1u, 1u, 1u};
        std::uint32_t inactivity_timeout_ms = 120'000u;
    };

    struct server_event
    {
        std::uint8_t channel_id = 0u;
        std::vector<std::uint8_t> cemi_bytes {};
    };

    class generic_server final
    {
    public:
        generic_server(datagram_transport& transport, server_config config = {},
                   server_clock_now_function clock_now = nullptr) noexcept;
        generic_server(const generic_server&) = delete;
        generic_server& operator=(const generic_server&) = delete;
        ~generic_server() noexcept = default;

        [[nodiscard]] task<std::expected<server_event, std::error_code>> serve_once() noexcept(false);
        [[nodiscard]] task_returning_expected_void_t serve() noexcept(false);
        [[nodiscard]] task_returning_expected_void_t send(
            std::uint8_t channel_id, std::span<const std::uint8_t> cemi_bytes) noexcept(false);
        [[nodiscard]] expected_void_t disconnect(std::uint8_t channel_id) noexcept;
        [[nodiscard]] expected_void_t shutdown() noexcept;
        [[nodiscard]] expected_void_t reset() noexcept;
        [[nodiscard]] expected_void_t poll() noexcept;

        [[nodiscard]] bool channel_active(std::uint8_t channel_id) const noexcept;
        [[nodiscard]] std::uint8_t active_channels() const noexcept;

    private:
        struct channel
        {
            bool active = false;
            transport_peer peer {};
            transport_peer data_peer {};
            hpai data_endpoint {};
            individual_address assigned_address {};
            std::uint8_t sequence = 0u;
            bool incoming_sequence_valid = false;
            std::uint8_t last_incoming_sequence = 0u;
            std::uint8_t next_incoming_sequence = 0u;
            std::uint32_t last_activity_ms = 0u;
        };

        [[nodiscard]] task_returning_expected_void_t send_datagram(
            const std::vector<std::uint8_t>& packet, const transport_peer& peer) noexcept(false);
        [[nodiscard]] bool peer_matches(const channel& value, const transport_peer& peer,
                        bool data_endpoint = false) const noexcept;
        [[nodiscard]] std::expected<std::uint8_t, std::error_code> allocate_channel() noexcept;
        [[nodiscard]] individual_address assigned_address(std::uint8_t channel_id) const noexcept;
        [[nodiscard]] std::uint32_t now_ms() const noexcept;
        void observe_activity(channel& value) noexcept;
        void release_channel(std::uint8_t channel_id) noexcept;

        datagram_transport& transport_;
        server_config config_ {};
        server_clock_now_function clock_now_ = nullptr;
        std::array<channel, 256u> channels_ {};
        std::array<std::uint8_t, frame::max_datagram_size> receive_buffer_ {};
        bool shutdown_ = false;
    };
}
