/// @file src/kmx/aio/modbus/error.cpp
/// @brief The Modbus error category instance and std::error_code creation for Modbus errors.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/modbus/error.hpp>
#ifndef PCH
    #include <kmx/aio/modbus/detail/category.hpp>
#endif

namespace kmx::aio::modbus
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
