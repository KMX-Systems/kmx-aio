/// @file src/kmx/aio/http3/frame.cpp
/// @brief HTTP/3 error category instance and std::error_code creation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/http3/frame.hpp>
#ifndef PCH
    #include <kmx/aio/http3/protocol_error_category.hpp>
#endif

namespace kmx::aio::http3
{
    const std::error_category& error_category() noexcept
    {
        static protocol_error_category category {};
        return category;
    }

    std::error_code make_error_code(const error_code code) noexcept
    {
        return {static_cast<int>(code), error_category()};
    }
}
