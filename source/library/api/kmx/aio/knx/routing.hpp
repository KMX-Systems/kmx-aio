/// @file aio/knx/routing.hpp
/// @brief Routing/multicast capability and indication boundary.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <array>
    #include <cstdint>
    #include <expected>
    #include <span>
    #include <system_error>
    #include <variant>
    #include <vector>
#endif

#include <kmx/aio/knx/error.hpp>
#include <kmx/aio/knx/frame.hpp>
#include <kmx/aio/knx/transport.hpp>

namespace kmx::aio::knx::routing
{
    using clock_now_function = std::uint32_t (*)() noexcept;
    inline constexpr std::uint16_t indication_service = 0x0530u;
    inline constexpr std::uint16_t lost_message_service = 0x0531u;
    inline constexpr std::uint16_t busy_service = 0x0532u;
    inline constexpr std::size_t indication_header_size = 4u;
    inline constexpr std::size_t control_body_size = 2u;
    using multicast_configuration = multicast_group_configuration;

    [[nodiscard]] constexpr std::expected<void, error> validate(
        const multicast_configuration& value) noexcept
    {
        if (value.port == 0u)
            return std::unexpected(error::invalid_configuration);
        if ((value.group[0u] < 224u) || (value.group[0u] > 239u))
            return std::unexpected(error::invalid_configuration);
        return {};
    }

    /// @brief Minimal routing indication envelope carried by a KNXnet/IP routing service.
    struct indication
    {
        std::uint8_t channel_id = 0u;
        std::span<const std::uint8_t> cemi_bytes {};
    };

    struct received_indication
    {
        std::uint8_t channel_id = 0u;
        std::vector<std::uint8_t> cemi_bytes {};
    };

    struct lost_message
    {
        std::uint16_t count = 0u;
    };

    struct busy
    {
        std::uint16_t wait_time_ms = 0u;
    };

    using event = std::variant<received_indication, lost_message, busy>;

    struct statistics
    {
        std::uint64_t busy_messages = 0u;
        std::uint64_t lost_messages = 0u;
        std::uint64_t reflected_messages = 0u;
        std::uint32_t busy_backoff_ms = 0u;
    };

    [[nodiscard]] std::expected<void, std::error_code> encode_indication_packet(
        span_uint8_t destination, const indication& value) noexcept;
    [[nodiscard]] std::expected<indication, std::error_code> decode_indication_packet(
        cspan_uint8_t packet) noexcept;
    [[nodiscard]] std::expected<void, std::error_code> encode_lost_message_packet(
        span_uint8_t destination, const lost_message& value) noexcept;
    [[nodiscard]] std::expected<lost_message, std::error_code> decode_lost_message_packet(
        cspan_uint8_t packet) noexcept;
    [[nodiscard]] std::expected<void, std::error_code> encode_busy_packet(
        span_uint8_t destination, const busy& value) noexcept;
    [[nodiscard]] std::expected<busy, std::error_code> decode_busy_packet(
        cspan_uint8_t packet) noexcept;

    class sender
    {
    public:
        sender() noexcept = default;
        sender(const sender&) = delete;
        sender& operator=(const sender&) = delete;
        virtual ~sender() noexcept = default;

        [[nodiscard]] virtual task_returning_expected_void_t send_indication(
            const indication& value) noexcept(false) = 0;
    };

    class client final: public sender
    {
    public:
        client(datagram_transport& transport,
                    multicast_configuration configuration = {},
                    clock_now_function clock_now = nullptr) noexcept:
                transport_(transport), configuration_(configuration), clock_now_(clock_now) {}

        [[nodiscard]] expected_void_t start() noexcept;
        [[nodiscard]] expected_void_t stop() noexcept;
        [[nodiscard]] const statistics& counters() const noexcept { return counters_; }
        void note_busy(const std::uint32_t backoff_ms) noexcept;
        void note_lost() noexcept;
        void note_reflected() noexcept;

        [[nodiscard]] task_returning_expected_void_t send_indication(
            const indication& value) noexcept(false) override;
        [[nodiscard]] task_returning_expected_void_t send_busy(
            const busy& value) noexcept(false);
        [[nodiscard]] task_returning_expected_void_t send_lost_message(
            const lost_message& value) noexcept(false);
        [[nodiscard]] task<std::expected<event, std::error_code>> receive_event() noexcept(false);
        [[nodiscard]] task<std::expected<received_indication, std::error_code>> receive_indication() noexcept(false);

    private:
        [[nodiscard]] std::expected<socket_address, std::error_code> multicast_peer() const noexcept;
        [[nodiscard]] static bool valid_source_peer(const transport_peer& peer) noexcept;
        [[nodiscard]] std::uint32_t now_ms() const noexcept;

        datagram_transport& transport_;
        multicast_configuration configuration_ {};
        bool started_ = false;
        statistics counters_ {};
        clock_now_function clock_now_ = nullptr;
        std::uint32_t busy_until_ms_ = 0u;
        std::vector<std::uint8_t> last_sent_packet_ {};
        std::array<std::uint8_t, frame::max_datagram_size> receive_buffer_ {};
    };
}
