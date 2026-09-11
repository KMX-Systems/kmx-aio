/// @file inc/kmx/aio/gpu/detail/cuda_category.hpp
/// @brief The std::error_category that names CUDA runtime error codes.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_CUDA)
    #ifndef PCH
        #include <system_error>
    #endif

namespace kmx::aio::gpu::detail
{
    /// @brief Returns the CUDA error category for use in std::system_error.
    /// @return The process-wide category instance.
    [[nodiscard]] const std::error_category& cuda_category() noexcept;
}
#endif
