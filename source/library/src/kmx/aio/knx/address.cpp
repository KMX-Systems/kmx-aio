/// @file kmx/aio/knx/address.cpp
/// @brief The two KNX address operations that allocate, and so cannot be constexpr.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/address.hpp>

namespace kmx::aio::knx
{
    std::string individual_address::to_string() const noexcept(false)
    {
        char text[detail::max_address_text_size] {};
        const auto size = format(text);
        return std::string {text, size.value_or(0u)};
    }

    std::string group_address::to_string(const group_address_style style) const noexcept(false)
    {
        char text[detail::max_address_text_size] {};
        const auto size = format(text, style);
        return std::string {text, size.value_or(0u)};
    }
}
