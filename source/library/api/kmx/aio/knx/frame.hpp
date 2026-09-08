/// @file aio/knx/frame.hpp
/// @brief Primitive KNXnet/IP frame and cEMI decode helpers.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <algorithm>
        #include <array>
        #include <cstdint>
        #include <expected>
        #include <span>
        #include <system_error>
        #include <vector>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/cemi.hpp>
    #include <kmx/aio/knx/contract.hpp>
    #include <kmx/aio/knx/error.hpp>

namespace kmx::aio::knx
{
    /// @brief The fixed six-octet header every KNXnet/IP datagram starts with.
    struct communication_header
    {
        /// @brief The protocol version; only `0x10` is defined.
        std::uint8_t protocol_version = 0x10u;
        /// @brief The service type identifier.
        std::uint16_t service_type {};
        /// @brief The datagram length including this header.
        std::uint16_t total_length {};
    };

    struct cemi_bytes_storage
    {
        std::array<std::uint8_t, cemi::max_l_data_size> bytes {};
        std::uint16_t size {};

        [[nodiscard]] constexpr bool empty() const noexcept { return size == 0u; }
        [[nodiscard]] constexpr std::size_t length() const noexcept { return size; }
        [[nodiscard]] constexpr auto begin() const noexcept { return bytes.begin(); }
        [[nodiscard]] constexpr auto end() const noexcept { return bytes.begin() + size; }
        [[nodiscard]] constexpr cspan_uint8_t span() const noexcept { return {bytes.data(), size}; }

        [[nodiscard]] friend bool operator==(const cemi_bytes_storage& lhs,
                                             const byte_buffer_t& rhs) noexcept
        {
            return (lhs.span().size() == rhs.size()) &&
                   std::equal(lhs.span().begin(), lhs.span().end(), rhs.begin());
        }
    };

    struct tunnelling_request_frame
    {
        std::uint8_t channel_id {};
        std::uint8_t sequence_number {};
        std::uint16_t message_length {};
        cemi_frame cemi {};
        cemi_bytes_storage cemi_bytes {};
    };

    struct tunnelling_ack_frame
    {
        std::uint8_t channel_id {};
        std::uint8_t sequence_number {};
        std::uint8_t status {};
    };

    namespace frame
    {
        inline constexpr std::uint16_t tunnelling_request_service = 0x0420u;
        inline constexpr std::uint16_t tunnelling_ack_service = 0x0421u;
        /// @brief Largest datagram the KNXnet/IP total length field can describe.
        inline constexpr std::size_t max_frame_size = 0xFFFFu;
        /// @brief Largest datagram this build buffers, the operational limit behind the protocol maximum.
        /// @details One IPv4 UDP payload on an Ethernet link, which is an order of magnitude more than any
        ///          KNXnet/IP service needs: the longest tunnelling frame a cEMI message can fill is about
        ///          530 octets. Buffering the protocol maximum instead would put 64 KiB on every coroutine
        ///          frame that sends a telegram of a couple of dozen bytes.
        inline constexpr std::size_t max_datagram_size = 1472u;
        inline constexpr std::size_t communication_header_size = 6u;
        inline constexpr std::size_t cemi_min_size = cemi::min_l_data_size;
        inline constexpr std::size_t tunnelling_request_header_size = 4u;
        inline constexpr std::size_t tunnelling_ack_size = 4u;

        [[nodiscard]] std::expected<communication_header, std::error_code> decode_communication_header(cspan_uint8_t buf) noexcept;
        [[nodiscard]] expected_void_t encode_communication_header(span_uint8_t dest,
                                                                                     std::uint16_t service_type,
                                                                                     std::uint16_t total_length,
                                                                                     std::uint8_t protocol_version = 0x10u) noexcept;
        [[nodiscard]] std::expected<cemi_frame, std::error_code> decode_cemi(cspan_uint8_t buf) noexcept;
        [[nodiscard]] expected_void_t encode_tunnelling_request(span_uint8_t dest,
                                                                                     std::uint8_t channel_id,
                                                                                     std::uint8_t sequence_number,
                                                                                     cspan_uint8_t cemi_bytes) noexcept;
        [[nodiscard]] std::expected<tunnelling_request_frame, std::error_code> decode_tunnelling_request(cspan_uint8_t buf) noexcept;
        [[nodiscard]] std::expected<tunnelling_ack_frame, std::error_code> decode_tunnelling_ack(cspan_uint8_t buf) noexcept;
        [[nodiscard]] expected_void_t encode_tunnelling_request_packet(
            span_uint8_t dest, std::uint8_t channel_id, std::uint8_t sequence_number, cspan_uint8_t cemi_bytes) noexcept;
        [[nodiscard]] expected_void_t encode_tunnelling_ack_packet(span_uint8_t dest, std::uint8_t channel_id,
                                                                                        std::uint8_t sequence_number,
                                                                                        std::uint8_t status = 0u) noexcept;
        [[nodiscard]] std::expected<tunnelling_request_frame, std::error_code> decode_tunnelling_request_packet(cspan_uint8_t buf) noexcept;
        [[nodiscard]] std::expected<tunnelling_ack_frame, std::error_code> decode_tunnelling_ack_packet(cspan_uint8_t buf) noexcept;
    }
}
#endif // KMX_AIO_FEATURE_KNX
