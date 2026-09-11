/// @file api/kmx/aio/http3/protocol_error_category.hpp
/// @brief HTTP/3 protocol error category.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_HTTP3)
    #ifndef PCH
        #include <string>
        #include <system_error>
    #endif

namespace kmx::aio::http3
{
    /// @brief `std::error_category` describing HTTP/3 protocol errors.
    class protocol_error_category final: public std::error_category
    {
    public:
        /// @brief Returns the stable error-category name.
        [[nodiscard]] const char* name() const noexcept override;

        /// @brief Returns the human-readable message for an HTTP/3 error code.
        /// @param ev The encoded error value.
        /// @return A descriptive error string.
        [[nodiscard]] std::string message(int ev) const override;
    };
}
#endif // KMX_AIO_FEATURE_HTTP3
