/// @file inc/kmx/aio/opc_ua/detail/category.hpp
/// @brief The std::error_category that names OPC UA wrapper-level error codes.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_OPC_UA)
    #ifndef PCH
        #include <string>
        #include <system_error>
    #endif

namespace kmx::aio::opc_ua::detail
{
    /// @brief The `std::error_category` that names @ref kmx::aio::opc_ua::error values.
    class category final: public std::error_category
    {
    public:
        /// @brief Returns the category name, "opc_ua".
        [[nodiscard]] const char* name() const noexcept override { return "opc_ua"; }

        /// @brief Returns the message text of an OPC UA error.
        /// @param ev The error value, a @ref kmx::aio::opc_ua::error.
        /// @return Its text, or "unknown OPC UA error" for a value outside the enumeration.
        [[nodiscard]] std::string message(int ev) const override;
    };
}
#endif // KMX_AIO_FEATURE_OPC_UA
