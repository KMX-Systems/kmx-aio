/// @file api/kmx/aio/invalid_argument.hpp
/// @brief Library exception for an argument outside the domain the callee accepts.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/exception.hpp>

    #include <stdexcept>
#endif

namespace kmx::aio
{
    /// @brief An argument outside the domain the callee accepts.
    class invalid_argument: public exception, public std::invalid_argument
    {
    public:
        using std::invalid_argument::invalid_argument;
    };
}
