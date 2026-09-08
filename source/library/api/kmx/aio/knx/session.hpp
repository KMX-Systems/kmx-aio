/// @file aio/knx/session.hpp
/// @brief Lightweight KNX tunnelling session state machine.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <chrono>
        #include <cstdint>
        #include <expected>
        #include <system_error>
        #include <vector>
    #endif

    #include <kmx/aio/knx/connection.hpp>
    #include <kmx/aio/knx/datagram.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/frame.hpp>

namespace kmx::aio::knx
{
    struct tunnelling_config
    {
        std::uint16_t max_retries = 2u;
        std::uint32_t ack_timeout_ms = 1000u;
        std::uint8_t heartbeat_failure_limit = 3u;
        std::uint32_t inactivity_timeout_ms = 120'000u;
    };

    enum class session_state : std::uint8_t
    {
        idle,
        connecting,
        connected,
        waiting_ack,
        closing,
        closed,
    };

    class tunnelling_session
    {
    public:
        constexpr tunnelling_session() noexcept = default;
        constexpr explicit tunnelling_session(const tunnelling_config& cfg) noexcept: config_(cfg) {}

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
        }

        [[nodiscard]] constexpr session_state state() const noexcept { return state_; }
        [[nodiscard]] constexpr bool has_pending_request() const noexcept { return pending_; }
        [[nodiscard]] constexpr std::uint32_t retries() const noexcept { return retries_; }
        [[nodiscard]] constexpr std::uint16_t channel_id() const noexcept { return channel_id_; }
        [[nodiscard]] constexpr std::uint16_t expected_sequence() const noexcept { return expected_sequence_; }
        [[nodiscard]] constexpr std::uint8_t next_sequence() const noexcept { return next_sequence_; }
        [[nodiscard]] constexpr std::uint8_t heartbeat_failures() const noexcept { return heartbeat_failures_; }
        [[nodiscard]] constexpr std::uint32_t last_activity_ms() const noexcept { return last_activity_ms_; }
        [[nodiscard]] constexpr std::uint32_t ack_timeout_ms() const noexcept { return config_.ack_timeout_ms; }
        [[nodiscard]] constexpr std::uint32_t deadline_ms() const noexcept { return last_deadline_ms_; }
        [[nodiscard]] constexpr individual_address assigned_address() const noexcept { return assigned_address_; }

        [[nodiscard]] cspan_uint8_t active_request_packet() const noexcept { return {request_packet_.data(), request_packet_.size()}; }

        [[nodiscard]] cspan_uint8_t active_connect_packet() const noexcept { return {connect_packet_.data(), connect_packet_.size()}; }

        constexpr void shutdown() noexcept
        {
            pending_ = false;
            state_ = session_state::closed;
            request_packet_.clear();
            connect_packet_.clear();
        }

        [[nodiscard]] expected_void_t prepare_connect_request_packet(const span_uint8_t packet,
                                                                                          const connect_request_frame& request) noexcept;

        [[nodiscard]] expected_void_t start_connect(const span_uint8_t packet, const connect_request_frame& request,
                                                                         const std::uint32_t deadline_ms) noexcept;

        [[nodiscard]] expected_void_t start_connect_raw(const cspan_uint8_t packet,
                                                                             const std::uint32_t deadline_ms) noexcept;

        [[nodiscard]] expected_void_t prepare_connect_retry_packet(const span_uint8_t packet) const noexcept;

        [[nodiscard]] expected_void_t begin_request(const std::uint16_t channel_id, const std::uint16_t sequence,
                                                                         const std::uint32_t deadline_ms) noexcept;

        [[nodiscard]] std::expected<std::uint8_t, std::error_code> begin_request(const std::uint8_t channel_id,
                                                                                 const std::uint32_t deadline_ms) noexcept;

        [[nodiscard]] std::expected<std::uint8_t, std::error_code> prepare_request_packet(const span_uint8_t packet,
                                                                                          const std::uint8_t channel_id,
                                                                                          const cspan_uint8_t cemi_bytes,
                                                                                          const std::uint32_t deadline_ms) noexcept;

        [[nodiscard]] expected_void_t prepare_retry_packet(const span_uint8_t packet) const noexcept;

        [[nodiscard]] expected_void_t prepare_connectionstate_request_packet(const span_uint8_t packet) const noexcept;

        [[nodiscard]] expected_void_t prepare_disconnect_request_packet(const span_uint8_t packet) noexcept;

        [[nodiscard]] expected_void_t on_disconnect_timeout() noexcept;

        [[nodiscard]] expected_void_t prepare_tunnelling_ack_packet(const span_uint8_t packet,
                                                                                         const tunnelling_request_frame& request,
                                                                                         const std::uint8_t status = 0u) const noexcept;

        [[nodiscard]] expected_void_t prepare_tunnelling_ack_packet(const span_uint8_t packet,
                                                                                         const cspan_uint8_t request_packet,
                                                                                         const std::uint8_t status = 0u) const noexcept;

        [[nodiscard]] expected_void_t prepare_response_datagram(const span_uint8_t packet, const datagram& request,
                                                                                     const std::uint8_t status = 0u) noexcept;

        [[nodiscard]] expected_void_t prepare_response_datagram(const span_uint8_t packet,
                                                                                     const cspan_uint8_t request_packet,
                                                                                     const std::uint8_t status = 0u) noexcept;

        [[nodiscard]] expected_void_t on_ack(const std::uint16_t channel_id, const std::uint16_t sequence) noexcept;

        [[nodiscard]] expected_void_t on_ack(const tunnelling_ack_frame& ack) noexcept;

        [[nodiscard]] expected_void_t on_connect_response(const connect_response_frame& response) noexcept;

        [[nodiscard]] expected_void_t on_connect_response(const ipv6_connect_response_frame& response) noexcept;

        [[nodiscard]] constexpr bool duplicate_indication(const tunnelling_request_frame& request) const noexcept
        {
            return (state_ == session_state::connected) && (request.channel_id == channel_id_) && incoming_sequence_valid_ &&
                   (request.sequence_number == last_incoming_sequence_);
        }

        [[nodiscard]] constexpr bool out_of_order_indication(const tunnelling_request_frame& request) const noexcept
        {
            return incoming_sequence_valid_ && (request.sequence_number != next_incoming_sequence_);
        }

        constexpr void note_activity(const std::uint32_t now_ms) noexcept
        {
            if (state_ == session_state::connected)
                last_activity_ms_ = now_ms;
        }

        constexpr void observe_activity(const std::uint32_t now_ms) noexcept { note_activity(now_ms); }

        [[nodiscard]] constexpr bool inactive(const std::uint32_t now_ms) const noexcept
        {
            return (state_ == session_state::connected) &&
                   (static_cast<std::int32_t>(now_ms - last_activity_ms_) >= static_cast<std::int32_t>(config_.inactivity_timeout_ms));
        }

        [[nodiscard]] expected_void_t check_inactivity(const std::uint32_t now_ms) noexcept;

        [[nodiscard]] expected_void_t on_connect_response_packet(const cspan_uint8_t packet) noexcept;

        [[nodiscard]] expected_void_t on_connect_timeout() noexcept;

        [[nodiscard]] constexpr bool connect_expired(const std::uint32_t now_ms) const noexcept
        {
            return (state_ == session_state::connecting) && (static_cast<std::int32_t>(now_ms - last_deadline_ms_) >= 0);
        }

        [[nodiscard]] expected_void_t on_connectionstate_response(const connectionstate_response_frame& response) noexcept;

        [[nodiscard]] expected_void_t on_connectionstate_response_packet(const cspan_uint8_t packet) noexcept;

        [[nodiscard]] expected_void_t on_connectionstate_timeout() noexcept;

        [[nodiscard]] expected_void_t on_ack_packet(const cspan_uint8_t packet) noexcept;

        [[nodiscard]] expected_void_t on_disconnect_response_packet(const cspan_uint8_t packet) noexcept;

        [[nodiscard]] expected_void_t on_datagram(const datagram& value) noexcept;

        [[nodiscard]] expected_void_t dispatch_session_datagram(const datagram& value,
                                                                                     const std::uint32_t now_ms) noexcept;

        [[nodiscard]] expected_void_t dispatch_session_datagram(const cspan_uint8_t packet,
                                                                                     const std::uint32_t now_ms) noexcept;

        [[nodiscard]] expected_void_t on_datagram(const cspan_uint8_t packet) noexcept;

        [[nodiscard]] expected_void_t on_datagram_at(const datagram& value, const std::uint32_t now_ms) noexcept
        {
            return dispatch_session_datagram(value, now_ms);
        }

        [[nodiscard]] expected_void_t on_datagram_at(const cspan_uint8_t packet, const std::uint32_t now_ms) noexcept
        {
            return dispatch_session_datagram(packet, now_ms);
        }

        [[nodiscard]] expected_void_t on_timeout() noexcept;

        [[nodiscard]] constexpr bool expired(const std::uint32_t now_ms) const noexcept
        {
            return pending_ && (static_cast<std::int32_t>(now_ms - last_deadline_ms_) >= 0);
        }

    private:
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
    };
}
#endif // KMX_AIO_FEATURE_KNX
