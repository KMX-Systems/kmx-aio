/// @file aio/http3/message.hpp
/// @brief HTTP/3 message definitions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace kmx::aio::http3
{
    /// @brief A single HTTP header field name/value pair.
    using header_field = std::pair<std::string, std::string>;
    /// @brief Ordered list of HTTP header fields.
    using header_list = std::vector<header_field>;

    /// @brief Logical HTTP/3 request metadata used by demo samples.
    struct request_head
    {
        /// @brief Request method.
        std::string method {"GET"};
        /// @brief Request scheme.
        std::string scheme {"https"};
        /// @brief Request authority.
        std::string authority {"localhost"};
        /// @brief Request target path.
        std::string target {"/"};
        /// @brief Additional request headers beyond the pseudo-headers.
        header_list headers {};
    };

    /// @brief Logical HTTP/3 response metadata used by demo samples.
    struct response_head
    {
        /// @brief HTTP status code.
        std::uint16_t status {200u};
        /// @brief Additional response headers.
        header_list headers {};
    };

    /// @brief Parsed demo request message.
    struct request_message
    {
        /// @brief Parsed request head.
        request_head head {};
        /// @brief Parsed request body.
        std::string body {};
    };

    /// @brief Parsed demo response message.
    struct response_message
    {
        /// @brief Parsed response head.
        response_head head {};
        /// @brief Parsed response body.
        std::string body {};
    };
} // namespace kmx::aio::http3