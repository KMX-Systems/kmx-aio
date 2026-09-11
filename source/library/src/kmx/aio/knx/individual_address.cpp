/// @file src/kmx/aio/knx/individual_address.cpp
/// @brief The KNX individual address operation that allocates, and so cannot be constexpr.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/individual_address.hpp>
#ifndef PCH
    #include <kmx/aio/knx/address.hpp>

    #include <string>
#endif

namespace kmx::aio::knx
{
    std::string individual_address::to_string() const noexcept(false)
    {
        char text[detail::max_address_text_size] {};
        const auto size = format(text);
        return std::string {text, size.value_or(0u)};
    }
}
