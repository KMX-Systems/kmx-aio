/// @file inc/kmx/aio/someip/detail/category.hpp
/// @brief The std::error_category that names SOME/IP wrapper-level error codes.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_SOMEIP)
    #ifndef PCH
        #include <string>
        #include <system_error>
    #endif

namespace kmx::aio::someip::detail
{
    /// @brief The `std::error_category` that names @ref kmx::aio::someip::error values.
    class category final: public std::error_category
    {
    public:
        /// @brief Returns the category name, "someip".
        [[nodiscard]] const char* name() const noexcept override { return "someip"; }

        /// @brief Returns the message text of a SOME/IP error.
        /// @param ev The error value, a @ref kmx::aio::someip::error.
        /// @return Its text, or "unknown SOME/IP error" for a value outside the enumeration.
        [[nodiscard]] std::string message(int ev) const override;
    };
}
#endif // KMX_AIO_FEATURE_SOMEIP
