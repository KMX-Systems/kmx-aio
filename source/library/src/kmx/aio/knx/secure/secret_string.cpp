/// @file src/kmx/aio/knx/secure/secret_string.cpp
/// @brief KNX Secure secret strings: a password buffer that wipes itself.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/secret_string.hpp>
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/secure/secret_bytes.hpp>

    #include <cstdint>
    #include <utility>
#endif

namespace kmx::aio::knx::secure
{
    secret_string::secret_string(const std::string_view text) noexcept(false): text_(text.begin(), text.end())
    {
    }

    secret_string& secret_string::operator=(secret_string&& other) noexcept
    {
        if (this != &other)
        {
            clear();
            text_ = std::move(other.text_);
            other.text_.clear();
        }

        return *this;
    }

    secret_string::~secret_string() noexcept
    {
        clear();
    }

    void secret_string::clear() noexcept
    {
        detail::cleanse(span_uint8_t {reinterpret_cast<std::uint8_t*>(text_.data()), text_.size()});
        text_.clear();
    }
}
