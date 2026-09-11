/// @file api/kmx/aio/http2/hpack.hpp
/// @brief HTTP/2 HPACK header field definitions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_HTTP2)
    #ifndef PCH
        #include <string_view>
        #include <utility>
        #include <vector>
    #endif

/// @brief HTTP/2 core protocol definitions and utilities
namespace kmx::aio::http2
{
    /// @brief One header field as a (name, value) pair of views into caller-owned storage.
    using header_field = std::pair<std::string_view, std::string_view>;
    /// @brief An ordered list of header fields, as it appears on the wire.
    using header_list = std::vector<header_field>;
}
#endif // KMX_AIO_FEATURE_HTTP2
