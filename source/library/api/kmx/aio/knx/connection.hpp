/// @file aio/knx/connection.hpp
/// @brief KNXnet/IP HPAI and tunnelling connection frame helpers.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <array>
        #include <cstdint>
        #include <expected>
        #include <span>
        #include <system_error>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/address.hpp>
    #include <kmx/aio/knx/frame.hpp>

namespace kmx::aio::knx
{
    enum class connect_status : std::uint8_t
    {
        no_error = 0x00u,
        host_protocol_type = 0x21u,
        version_not_supported = 0x22u,
        sequence_number = 0x23u,
        connection_type = 0x24u,
        connection_option = 0x25u,
        no_more_connections = 0x26u,
    };

    struct ipv4_endpoint
    {
        std::array<std::uint8_t, 4u> address {};
        std::uint16_t port {};
    };

    struct ipv6_endpoint
    {
        std::array<std::uint8_t, 16u> address {};
        std::uint16_t port {};
    };

    struct hpai
    {
        ipv4_endpoint endpoint {};
        std::uint8_t protocol = 0x01u;
    };

    struct ipv6_hpai
    {
        ipv6_endpoint endpoint {};
        std::uint8_t protocol = 0x01u;
    };

    struct ipv6_connect_request_frame
    {
        ipv6_hpai control_endpoint {};
        ipv6_hpai data_endpoint {};
    };

    struct ipv6_connect_response_frame
    {
        std::uint8_t channel_id {};
        connect_status status = connect_status::no_error;
        ipv6_hpai data_endpoint {};
        individual_address assigned_address {};
    };

    struct connect_request_frame
    {
        hpai control_endpoint {};
        hpai data_endpoint {};
    };

    struct connect_response_frame
    {
        std::uint8_t channel_id {};
        connect_status status = connect_status::no_error;
        hpai data_endpoint {};
        /// @brief The individual address the interface assigned to this tunnel, from the CRD.
        /// @note The interface substitutes this address into every frame the client sends with an unset
        ///       source, which is why a tunnelling client has no address of its own to configure.
        individual_address assigned_address {};
    };

    struct connectionstate_request_frame
    {
        std::uint8_t channel_id {};
    };

    struct connectionstate_response_frame
    {
        std::uint8_t channel_id {};
        connect_status status = connect_status::no_error;
    };

    struct disconnect_request_frame
    {
        std::uint8_t channel_id {};
    };

    struct disconnect_response_frame
    {
        std::uint8_t channel_id {};
        connect_status status = connect_status::no_error;
    };

    namespace connection
    {
        inline constexpr std::uint16_t connect_request_service = 0x0205u;
        inline constexpr std::uint16_t connect_response_service = 0x0206u;
        inline constexpr std::uint16_t connectionstate_request_service = 0x0207u;
        inline constexpr std::uint16_t connectionstate_response_service = 0x0208u;
        inline constexpr std::uint16_t disconnect_request_service = 0x0209u;
        inline constexpr std::uint16_t disconnect_response_service = 0x020Au;
        inline constexpr std::size_t hpai_size = 8u;
        inline constexpr std::size_t ipv6_hpai_size = 20u;
        inline constexpr std::size_t connect_request_body_size = 20u;
        inline constexpr std::size_t connect_response_body_size = 14u;
        inline constexpr std::size_t ipv6_connect_request_body_size = 44u;
        inline constexpr std::size_t ipv6_connect_response_body_size = 26u;
        /// @brief Structure length of the tunnelling connection request and response information blocks.
        inline constexpr std::uint8_t connection_information_size = 0x04u;
        /// @brief Connection type code of a tunnelling connection.
        inline constexpr std::uint8_t tunnel_connection_type = 0x04u;
        /// @brief KNX layer code of a link layer tunnel, the only layer this build requests.
        inline constexpr std::uint8_t tunnel_link_layer = 0x02u;

        [[nodiscard]] expected_void_t encode_connect_request_packet(span_uint8_t dest, const connect_request_frame& request) noexcept;
        [[nodiscard]] std::expected<connect_request_frame, std::error_code> decode_connect_request_packet(
            cspan_uint8_t packet) noexcept;
        [[nodiscard]] expected_void_t encode_ipv6_connect_request_packet(
            span_uint8_t dest, const ipv6_connect_request_frame& request) noexcept;
        [[nodiscard]] std::expected<ipv6_connect_request_frame, std::error_code> decode_ipv6_connect_request_packet(
            cspan_uint8_t packet) noexcept;
        [[nodiscard]] expected_void_t encode_connect_response_packet(span_uint8_t dest, const connect_response_frame& response) noexcept;
        [[nodiscard]] std::expected<connect_response_frame, std::error_code> decode_connect_response_packet(
            cspan_uint8_t packet) noexcept;
        [[nodiscard]] expected_void_t encode_ipv6_connect_response_packet(
            span_uint8_t dest, const ipv6_connect_response_frame& response) noexcept;
        [[nodiscard]] std::expected<ipv6_connect_response_frame, std::error_code> decode_ipv6_connect_response_packet(
            cspan_uint8_t packet) noexcept;
        [[nodiscard]] expected_void_t encode_connectionstate_request_packet(
            span_uint8_t dest, const connectionstate_request_frame& request) noexcept;
        [[nodiscard]] std::expected<connectionstate_request_frame, std::error_code> decode_connectionstate_request_packet(
            cspan_uint8_t packet) noexcept;
        [[nodiscard]] expected_void_t encode_connectionstate_response_packet(
            span_uint8_t dest, const connectionstate_response_frame& response) noexcept;
        [[nodiscard]] std::expected<connectionstate_response_frame, std::error_code> decode_connectionstate_response_packet(
            cspan_uint8_t packet) noexcept;
        [[nodiscard]] expected_void_t encode_disconnect_request_packet(
            span_uint8_t dest, const disconnect_request_frame& request) noexcept;
        [[nodiscard]] std::expected<disconnect_request_frame, std::error_code> decode_disconnect_request_packet(
            cspan_uint8_t packet) noexcept;
        [[nodiscard]] expected_void_t encode_disconnect_response_packet(
            span_uint8_t dest, const disconnect_response_frame& response) noexcept;
        [[nodiscard]] std::expected<disconnect_response_frame, std::error_code> decode_disconnect_response_packet(
            cspan_uint8_t packet) noexcept;
    }
}
#endif // KMX_AIO_FEATURE_KNX
