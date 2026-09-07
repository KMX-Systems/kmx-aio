/// @file aio/knx/discovery.hpp
/// @brief KNXnet/IP discovery frame helpers.
#pragma once
#ifndef PCH
#include <array>
    #include <cstdint>
    #include <expected>
    #include <system_error>
#include <variant>
    #include <vector>
#endif

#include <kmx/aio/basic_types.hpp>
#include <kmx/aio/knx/connection.hpp>
#include <kmx/aio/knx/transport.hpp>

namespace kmx::aio::knx::discovery
{
    inline constexpr std::uint16_t search_request_service = 0x0201u;
    inline constexpr std::uint16_t search_response_service = 0x0202u;
    inline constexpr std::uint16_t description_request_service = 0x0203u;
    inline constexpr std::uint16_t description_response_service = 0x0204u;
    inline constexpr std::size_t search_request_body_size = connection::hpai_size;
    inline constexpr std::size_t ipv6_search_request_body_size = connection::ipv6_hpai_size;

    struct search_request_frame
    {
        hpai discovery_endpoint {};
    };

    struct ipv6_search_request_frame
    {
        ipv6_hpai discovery_endpoint {};
    };

    struct search_response_frame
    {
        hpai control_endpoint {};
        std::vector<std::uint8_t> device_info_blocks {};
    };

    struct ipv6_search_response_frame
    {
        ipv6_hpai control_endpoint {};
        std::vector<std::uint8_t> device_info_blocks {};
    };

    struct description_request_frame {};

    struct description_response_frame
    {
        std::vector<std::uint8_t> device_info_blocks {};
    };

    using search_response = std::variant<search_response_frame, ipv6_search_response_frame>;

    class client final
    {
    public:
        client(datagram_transport& transport, const sockaddr_storage& peer, ::socklen_t peer_length) noexcept:
            transport_(transport), peer_(peer), peer_length_(peer_length) {}

        [[nodiscard]] task<std::expected<search_response, std::error_code>> search(
            const search_request_frame& request) noexcept(false);
        [[nodiscard]] task<std::expected<search_response, std::error_code>> search(
            const ipv6_search_request_frame& request) noexcept(false);

    private:
        [[nodiscard]] task<std::expected<search_response, std::error_code>> search_packet(
            cspan_uint8_t packet) noexcept(false);
        [[nodiscard]] bool peer_matches(const transport_peer& peer) const noexcept;

        datagram_transport& transport_;
        sockaddr_storage peer_ {};
        ::socklen_t peer_length_ = 0u;
        std::array<std::uint8_t, frame::max_datagram_size> buffer_ {};
    };

    [[nodiscard]] std::expected<void, std::error_code> encode_search_request_packet(
        span_uint8_t dest, const search_request_frame& request) noexcept;
    [[nodiscard]] std::expected<search_request_frame, std::error_code> decode_search_request_packet(
        cspan_uint8_t packet) noexcept;
    [[nodiscard]] std::expected<void, std::error_code> encode_ipv6_search_request_packet(
        span_uint8_t dest, const ipv6_search_request_frame& request) noexcept;
    [[nodiscard]] std::expected<ipv6_search_request_frame, std::error_code> decode_ipv6_search_request_packet(
        cspan_uint8_t packet) noexcept;
    [[nodiscard]] std::expected<void, std::error_code> encode_search_response_packet(
        span_uint8_t dest, const search_response_frame& response) noexcept;
    [[nodiscard]] std::expected<search_response_frame, std::error_code> decode_search_response_packet(
        cspan_uint8_t packet) noexcept;
    [[nodiscard]] std::expected<void, std::error_code> encode_ipv6_search_response_packet(
        span_uint8_t dest, const ipv6_search_response_frame& response) noexcept;
    [[nodiscard]] std::expected<ipv6_search_response_frame, std::error_code> decode_ipv6_search_response_packet(
        cspan_uint8_t packet) noexcept;
    [[nodiscard]] std::expected<void, std::error_code> encode_description_request_packet(
        span_uint8_t dest) noexcept;
    [[nodiscard]] std::expected<description_request_frame, std::error_code> decode_description_request_packet(
        cspan_uint8_t packet) noexcept;
    [[nodiscard]] std::expected<void, std::error_code> encode_description_response_packet(
        span_uint8_t dest, const description_response_frame& response) noexcept;
    [[nodiscard]] std::expected<description_response_frame, std::error_code> decode_description_response_packet(
        cspan_uint8_t packet) noexcept;
}
