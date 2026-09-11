/// @file src/kmx/aio/knx/error.cpp
/// @brief The KNX error category instance and std::error_code creation for the wrapper-level error codes.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/error.hpp>
#ifndef PCH
    #include <kmx/aio/knx/detail/category.hpp>
#endif

namespace kmx::aio::knx
{
    namespace detail
    {
        const category category_instance {};
    }

    const std::error_category& error_category() noexcept
    {
        return detail::category_instance;
    }

    std::error_code make_error_code(const error code) noexcept
    {
        return {static_cast<int>(code), error_category()};
    }
}
