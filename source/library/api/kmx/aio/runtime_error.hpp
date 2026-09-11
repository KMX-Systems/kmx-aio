/// @file api/kmx/aio/runtime_error.hpp
/// @brief Library exception for an unrecoverable runtime condition.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/exception.hpp>

    #include <stdexcept>
#endif

namespace kmx::aio
{
    /// @brief An unrecoverable runtime condition, detectable only while the operation runs.
    class runtime_error: public exception, public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };
}
