/// @file inc/kmx/aio/modbus/detail/category.hpp
/// @brief The std::error_category that names Modbus error codes.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_MODBUS)
    #ifndef PCH
        #include <string>
        #include <system_error>
    #endif

namespace kmx::aio::modbus::detail
{
    /// @brief The `std::error_category` that names @ref kmx::aio::modbus::error values.
    class category final: public std::error_category
    {
    public:
        /// @brief Returns the category name, "modbus".
        [[nodiscard]] const char* name() const noexcept override { return "modbus"; }

        /// @brief Returns the message text of a Modbus error.
        /// @param ev The error value, a @ref kmx::aio::modbus::error.
        /// @return Its text, or "unknown Modbus error" for a value outside the enumeration.
        [[nodiscard]] std::string message(int ev) const override;
    };
}
#endif // KMX_AIO_FEATURE_MODBUS
