/// @file api/kmx/aio/logic_error.hpp
/// @brief Library exception for a precondition the caller violated.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/exception.hpp>

    #include <stdexcept>
#endif

namespace kmx::aio
{
    /// @brief A violated precondition, detectable by the caller before the call.
    class logic_error: public exception, public std::logic_error
    {
    public:
        using std::logic_error::logic_error;
    };
}
