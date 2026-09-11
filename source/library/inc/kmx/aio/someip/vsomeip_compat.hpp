/// @file inc/kmx/aio/someip/vsomeip_compat.hpp
/// @brief Internal backend abstraction layer between the SOME/IP facade and vsomeip: the vsomeip headers and the RPC message.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/someip/types.hpp>

    #include <cstdint>
    #include <vector>
#endif

#if defined(KMX_AIO_FEATURE_SOMEIP) && defined(KMX_AIO_SOMEIP_LINK_BACKEND)
    #if __has_include(<vsomeip/vsomeip.hpp>)
        #define KMX_AIO_HAS_VSOMEIP_HEADER 1
    #elif __has_include(<vsomeip3/vsomeip.hpp>)
        #define KMX_AIO_HAS_VSOMEIP_HEADER 1
    #endif
#endif

#if defined(KMX_AIO_HAS_VSOMEIP_HEADER)
    #if defined(__clang__)
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wunused-parameter"
    #elif defined(__GNUC__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wunused-parameter"
    #endif

    #if __has_include(<vsomeip/vsomeip.hpp>)
        #include <vsomeip/vsomeip.hpp>
    #else
        #include <vsomeip3/vsomeip.hpp>
    #endif

    #if defined(__clang__)
        #pragma clang diagnostic pop
    #elif defined(__GNUC__)
        #pragma GCC diagnostic pop
    #endif
#endif

/// @namespace kmx::aio::someip::vsomeip_compat
/// @brief Internal backend abstraction; not part of the public SOME/IP API.
namespace kmx::aio::someip::vsomeip_compat
{
    /// @brief Represents a single SOME/IP RPC message (request or response).
    struct rpc_message
    {
        /// @brief Service identifier.
        service_id_t service_id {};

        /// @brief Instance identifier.
        instance_id_t instance_id {};

        /// @brief Method identifier.
        method_id_t method_id {};

        /// @brief Composite request identifier (client-id | session-id).
        request_id_t request_id {};

        /// @brief Payload bytes.
        std::vector<std::uint8_t> payload;
    };

}
