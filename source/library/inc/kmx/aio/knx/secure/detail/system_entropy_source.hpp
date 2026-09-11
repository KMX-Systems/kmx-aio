/// @file inc/kmx/aio/knx/secure/detail/system_entropy_source.hpp
/// @brief The KNX Secure entropy source backed by the project's TLS backend.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/secure/entropy_source.hpp>
        #include <kmx/aio/knx/secure/key.hpp>
    #endif

namespace kmx::aio::knx::secure::detail
{
    /// @brief The entropy source backed by the project's TLS backend.
    class system_entropy_source final: public entropy_source
    {
    public:
        [[nodiscard]] expected_void_t fill(span_uint8_t destination) noexcept override;

        [[nodiscard]] x25519_key_pair_result_t generate_key_pair() noexcept override;
    };
}
#endif // KMX_AIO_FEATURE_KNX
