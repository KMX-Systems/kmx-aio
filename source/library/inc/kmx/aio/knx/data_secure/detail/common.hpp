/// @file inc/kmx/aio/knx/data_secure/detail/common.hpp
/// @brief What the KNX Data Secure codec and context share: the A_SecureService APCI octets, and how they refuse.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/knx/error.hpp>

        #include <cstdint>
        #include <expected>
        #include <system_error>
    #endif

namespace kmx::aio::knx::data_secure::detail
{
    /// @brief The second APCI octet of A_SecureService.
    inline constexpr std::uint8_t service_apci_low = 0xF1u;
    /// @brief The two APCI bits of A_SecureService in the first APDU octet.
    inline constexpr std::uint8_t service_apci_high = 0x03u;

    /// @brief Returns the refusal carrying @p reason.
    [[nodiscard]] inline std::unexpected<std::error_code> refuse(const error reason) noexcept
    {
        return std::unexpected(make_error_code(reason));
    }
}
#endif // KMX_AIO_FEATURE_KNX
