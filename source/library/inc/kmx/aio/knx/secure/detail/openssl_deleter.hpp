/// @file inc/kmx/aio/knx/secure/detail/openssl_deleter.hpp
/// @brief A std::unique_ptr deleter that frees an OpenSSL or BoringSSL object through the backend's own free function.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)

namespace kmx::aio::knx::secure::detail
{
    /// @brief Frees a backend object with the function the backend provides for its type.
    /// @tparam Free The free function, such as `&EVP_PKEY_free`.
    template <auto Free>
    struct openssl_deleter
    {
        /// @brief Frees @p object.
        /// @tparam Object The backend type @p Free takes.
        /// @param object The object to free.
        template <typename Object>
        void operator()(Object* const object) const noexcept
        {
            Free(object);
        }
    };
}
#endif // KMX_AIO_FEATURE_KNX
