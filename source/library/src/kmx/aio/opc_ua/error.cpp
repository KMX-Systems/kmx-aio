/// @file src/kmx/aio/opc_ua/error.cpp
/// @brief The OPC UA error category instance and std::error_code creation for OPC UA errors.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/opc_ua/error.hpp>
#ifndef PCH
    #include <kmx/aio/opc_ua/detail/category.hpp>
#endif

namespace kmx::aio::opc_ua
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
