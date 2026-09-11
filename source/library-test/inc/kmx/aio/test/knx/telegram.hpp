/// @file inc/kmx/aio/test/knx/telegram.hpp
/// @brief Well-formed cEMI telegrams shared by the KNX tests.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// The KNXnet/IP framing tests need a cEMI payload that decodes, not an arbitrary run of octets. These
/// helpers give them one, built through the same encoder the library ships and cross-checked against the
/// compile-time vectors in `kmx/aio/knx/detail/codec_vectors.hpp`, so a test can never pass against a
/// telegram no interface would accept.
#pragma once
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/knx/detail/codec_vectors.hpp>
        #include <kmx/aio/knx/frame.hpp>

        #include <array>
        #include <cstdint>
        #include <vector>
    #endif

namespace kmx::aio::test::knx
{
    /// @brief Size of the smallest well-formed cEMI L_Data telegram.
    inline constexpr std::size_t sample_cemi_size = kmx::aio::knx::cemi::min_l_data_size;

    /// @brief An A_GroupValue_Write switch-on telegram from 1.1.1 to 1/2/3.
    inline constexpr std::array<std::uint8_t, sample_cemi_size> sample_cemi = kmx::aio::knx::detail::codec_vectors::group_value_write_on;

    /// @brief Size of a TUNNELLING_REQUEST datagram carrying @ref sample_cemi.
    inline constexpr std::size_t sample_tunnelling_packet_size =
        kmx::aio::knx::frame::communication_header_size + kmx::aio::knx::frame::tunnelling_request_header_size + sample_cemi_size;

    /// @brief An A_GroupValue_Read telegram from 1.1.1 to 1/2/3, distinct from @ref sample_cemi.
    inline constexpr std::array<std::uint8_t, sample_cemi_size> sample_cemi_read = kmx::aio::knx::detail::codec_vectors::group_value_read;

    /// @brief An A_GroupValue_Write telegram carrying 21.5 degrees as a 2-octet KNX float.
    inline constexpr std::array<std::uint8_t, 13u> sample_cemi_temperature =
        kmx::aio::knx::detail::codec_vectors::group_value_write_temperature;

    /// @brief Builds a cEMI telegram whose payload is one octet shorter than the smallest valid one.
    /// @return The truncated octets, which every decoder must reject.
    [[nodiscard]] inline std::vector<std::uint8_t> truncated_cemi() noexcept(false)
    {
        return {sample_cemi.begin(), sample_cemi.end() - 1u};
    }
}
#endif
