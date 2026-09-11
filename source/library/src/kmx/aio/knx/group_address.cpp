/// @file src/kmx/aio/knx/group_address.cpp
/// @brief The KNX group address operation that allocates, and so cannot be constexpr.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/group_address.hpp>
#ifndef PCH
    #include <kmx/aio/knx/address.hpp>

    #include <string>
#endif

namespace kmx::aio::knx
{
    std::string group_address::to_string(const group_address_style style) const noexcept(false)
    {
        char text[detail::max_address_text_size] {};
        const auto size = format(text, style);
        return std::string {text, size.value_or(0u)};
    }
}
