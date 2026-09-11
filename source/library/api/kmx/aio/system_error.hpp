/// @file api/kmx/aio/system_error.hpp
/// @brief Library exception for a failed operating system or third-party library call.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/exception.hpp>

    #include <system_error>
#endif

namespace kmx::aio
{
    /// @brief A failed operating system or third-party library call, carrying the code it reported.
    /// @note Inherits the `std::system_error` constructors, so a `std::error_code`, or an `int` together
    ///       with its `std::error_category`, is the way to build one.
    class system_error: public exception, public std::system_error
    {
    public:
        using std::system_error::system_error;
    };
}
