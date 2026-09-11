/// @file api/kmx/aio/http3/qpack.hpp
/// @brief HTTP/3 QPACK definitions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_HTTP3)
    #ifndef PCH
        #include <cstdint>
    #endif

namespace kmx::aio::http3::qpack
{
    /// @brief Encoded field representation tags used by the demo literal codec.
    enum class field_representation : std::uint8_t
    {
        /// @brief Literal field with inline name.
        literal_with_name = 0x00u,
        /// @brief Literal field with indexed name.
        literal_with_name_ref = 0x40u,
        /// @brief Indexed field from the static table.
        indexed_field = 0x80u,
    };
}
#endif // KMX_AIO_FEATURE_HTTP3
