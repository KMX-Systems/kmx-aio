/// @file aio/knx/client.hpp
/// @brief Coroutine-oriented KNXnet/IP tunnelling client boundary.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <atomic>
        #include <array>
        #include <cstdint>
        #include <expected>
        #include <span>
        #include <sys/socket.h>
        #include <vector>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/task.hpp>
    #include <kmx/aio/knx/cemi.hpp>
    #include <kmx/aio/knx/connection.hpp>
    #include <kmx/aio/knx/dpt.hpp>
    #include <kmx/aio/knx/session.hpp>
    #include <kmx/aio/knx/secure.hpp>
    #include <kmx/aio/knx/transport.hpp>

namespace kmx::aio::knx
{
    using clock_now_function = std::uint32_t (*)() noexcept;

    /// @brief A received cEMI message together with the octets it was decoded from.
    /// @details The decoded frame names its payload by offset, so the octets have to travel with it. Owning
    ///          them here is what lets a telegram outlive the receive buffer it arrived in.
    struct telegram
    {
        /// @brief The decoded message.
        cemi_frame frame {};
        /// @brief The cEMI octets the message was decoded from.
        byte_buffer_t bytes {};

        /// @brief Returns the application payload.
        [[nodiscard]] cspan_uint8_t payload() const noexcept { return frame.payload(bytes); }
        /// @brief Returns the value view a datapoint decoder accepts.
        [[nodiscard]] dpt::value_view value() const noexcept { return dpt::make_value_view(frame, bytes); }

        /// @brief Decodes the application value as the named datapoint main type.
        /// @tparam Main The datapoint main type.
        /// @return The decoded value, or the reason it could not be decoded.
        template <std::uint16_t Main>
        [[nodiscard]] dpt::decode_result_t<Main> value_as() const noexcept
        {
            return dpt::traits<Main>::decode(value());
        }
    };

    /// @brief A received telegram, or the error explaining why none was obtained.
    using telegram_result_t = std::expected<telegram, std::error_code>;
    /// @brief Task yielding a received telegram or the error that stopped the receive.
    using telegram_task_t = task<telegram_result_t>;
    /// @brief Task yielding raw cEMI octets or the error that stopped the receive.
    using cemi_bytes_task_t = task<expected_byte_buffer_t>;

    class tunnelling_client final
    {
    public:
        tunnelling_client(datagram_transport& transport,
                          const sockaddr_storage& peer,
                          ::socklen_t peer_length,
                          tunnelling_config config = {},
                          clock_now_function clock_now = nullptr,
                          secure::configuration secure_config = {},
                          secure::provider* secure_provider = nullptr) noexcept;
        tunnelling_client(const tunnelling_client&) = delete;
        tunnelling_client& operator=(const tunnelling_client&) = delete;
        ~tunnelling_client() noexcept = default;

        [[nodiscard]] session_state state() const noexcept { return session_.state(); }
        [[nodiscard]] bool connected() const noexcept { return state() == session_state::connected; }
        [[nodiscard]] bool closing() const noexcept { return state() == session_state::closing; }
        [[nodiscard]] bool closed() const noexcept { return state() == session_state::closed; }
        [[nodiscard]] std::uint32_t last_activity_ms() const noexcept { return session_.last_activity_ms(); }
        [[nodiscard]] expected_void_t poll() noexcept
        {
            return session_.check_inactivity(now_ms());
        }
        [[nodiscard]] std::uint8_t channel_id() const noexcept
        {
            return static_cast<std::uint8_t>(session_.channel_id());
        }
        /// @brief Returns the individual address the interface assigned to this tunnel.
        /// @note Unset until a CONNECT_RESPONSE has been accepted.
        [[nodiscard]] individual_address assigned_address() const noexcept { return session_.assigned_address(); }

        [[nodiscard]] task_returning_expected_void_t connect(
            const connect_request_frame& request) noexcept(false);
        [[nodiscard]] task_returning_expected_void_t connect(
            const ipv6_connect_request_frame& request) noexcept(false);
        [[nodiscard]] task_returning_expected_void_t send(
            cspan_uint8_t cemi_bytes) noexcept(false);
        [[nodiscard]] expected_byte_buffer_t protect_payload(cspan_uint8_t payload, std::uint64_t sequence) const noexcept;
        [[nodiscard]] expected_byte_buffer_t unprotect_payload(cspan_uint8_t payload, std::uint64_t sequence) const noexcept;
        [[nodiscard]] task_returning_expected_void_t heartbeat() noexcept(false);
        [[nodiscard]] datagram_task_t receive_datagram() noexcept(false);
        [[nodiscard]] cemi_bytes_task_t receive_cemi() noexcept(false);
        [[nodiscard]] telegram_task_t receive_telegram() noexcept(false);
        [[nodiscard]] task_returning_expected_void_t disconnect() noexcept(false);

        /// @brief Sends an A_GroupValue_Write telegram.
        /// @param destination The group to write to.
        /// @param value The value to write, as encoded by the datapoint layer.
        /// @param options The link layer flags.
        /// @return Nothing, or the reason the telegram could not be sent.
        /// @note The source address is left unset so the interface substitutes the address it assigned.
        [[nodiscard]] task_returning_expected_void_t write_group_value(group_address destination, const dpt::payload& value,
                                                                      l_data_options options = {}) noexcept(false);

        /// @brief Sends an A_GroupValue_Read telegram.
        /// @param destination The group to read from.
        /// @param options The link layer flags.
        /// @return Nothing, or the reason the telegram could not be sent.
        [[nodiscard]] task_returning_expected_void_t read_group_value(group_address destination,
                                                                     l_data_options options = {}) noexcept(false);

        /// @brief Sends an A_GroupValue_Response telegram.
        /// @param destination The group the response belongs to.
        /// @param value The value to report, as encoded by the datapoint layer.
        /// @param options The link layer flags.
        /// @return Nothing, or the reason the telegram could not be sent.
        [[nodiscard]] task_returning_expected_void_t respond_group_value(group_address destination, const dpt::payload& value,
                                                                        l_data_options options = {}) noexcept(false);

        void shutdown() noexcept;

        void reset() noexcept;

    private:
        class operation_guard
        {
        public:
            explicit operation_guard(tunnelling_client& owner) noexcept;
            ~operation_guard() noexcept;
            operation_guard(const operation_guard&) = delete;
            operation_guard& operator=(const operation_guard&) = delete;

            [[nodiscard]] bool acquired() const noexcept { return acquired_; }

        private:
            tunnelling_client* owner_ {};
            bool acquired_ {};
        };

        struct received_cemi
        {
            cemi_frame frame {};
            byte_buffer_t bytes {};
        };

        /// @brief A received cEMI message, or the error explaining why none was obtained.
        using received_cemi_result_t = std::expected<received_cemi, std::error_code>;
        /// @brief Task yielding a received cEMI message or the error that stopped the receive.
        using received_cemi_task_t = task<received_cemi_result_t>;

        enum class endpoint_kind : std::uint8_t
        {
            control,
            data,
        };

        [[nodiscard]] bool peer_matches(const transport_peer& peer, endpoint_kind kind) const noexcept;
        void clear_data_peer() noexcept;
        [[nodiscard]] std::uint32_t now_ms() const noexcept;
        [[nodiscard]] std::uint32_t operation_deadline_ms() const noexcept;
        [[nodiscard]] task_returning_expected_void_t send_packet(
            cspan_uint8_t packet, endpoint_kind kind) noexcept(false);
        [[nodiscard]] datagram_result_t decode_received_packet(cspan_uint8_t packet, endpoint_kind kind) noexcept;
        [[nodiscard]] expected_byte_buffer_t secure_wrap_data_packet(cspan_uint8_t packet, std::uint64_t sequence) const noexcept;
        [[nodiscard]] bool secure_data_enabled() const noexcept
        {
            return secure_config_.selected != secure::profile::none;
        }
        [[nodiscard]] std::uint64_t next_secure_sequence() noexcept { return secure_sequence_++; }
        void reset_secure_state() noexcept
        {
            secure_sequence_ = 1u;
            secure_replay_ = secure::replay_window_state {secure_config_.replay_window};
        }
        [[nodiscard]] datagram_task_t receive_datagram_impl() noexcept(false);
        [[nodiscard]] received_cemi_task_t receive_cemi_impl() noexcept(false);
        [[nodiscard]] task_returning_expected_void_t receive_into_session(endpoint_kind kind) noexcept(false);
        [[nodiscard]] task_returning_expected_void_t send_group_service(group_address destination, apci service, const apdu_payload& value,
                                                                       const l_data_options& options) noexcept(false);

        datagram_transport& transport_;
        sockaddr_storage peer_ {};
        ::socklen_t peer_length_ = 0u;
        bool configured_peer_valid_ {};
        sockaddr_storage data_peer_ {};
        ::socklen_t data_peer_length_ = 0u;
        bool data_peer_valid_ {};
        clock_now_function clock_now_ {};
        secure::configuration secure_config_ {};
        secure::provider* secure_provider_ {};
        std::uint64_t secure_sequence_ = 1u;
        secure::replay_window_state secure_replay_ {};
        std::atomic_bool operation_active_ {};
        tunnelling_session session_;
        std::array<std::uint8_t, frame::max_datagram_size> receive_buffer_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
