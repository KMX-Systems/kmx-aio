/// @file inc/kmx/aio/knx/detail/category.hpp
/// @brief The std::error_category that names KNX wrapper-level error codes.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <string>
        #include <system_error>
    #endif

namespace kmx::aio::knx::detail
{
    /// @brief The `std::error_category` that names @ref kmx::aio::knx::error values.
    class category final: public std::error_category
    {
    public:
        /// @brief Returns the category name, "knx".
        [[nodiscard]] const char* name() const noexcept override { return "knx"; }

        /// @brief Returns the message text of a KNX error.
        /// @param ev The error value, a @ref kmx::aio::knx::error.
        /// @return Its text, or "unknown KNX error" for a value outside the enumeration.
        [[nodiscard]] std::string message(int ev) const override;
    };
}
#endif // KMX_AIO_FEATURE_KNX
