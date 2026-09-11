/// @file src/kmx/aio/someip/error.cpp
/// @brief The SOME/IP error category instance and std::error_code creation for SOME/IP errors.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/someip/error.hpp>
#ifndef PCH
    #include <kmx/aio/someip/detail/category.hpp>
#endif

namespace kmx::aio::someip
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
