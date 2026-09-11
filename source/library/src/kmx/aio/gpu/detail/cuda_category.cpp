/// @file src/kmx/aio/gpu/detail/cuda_category.cpp
/// @brief The CUDA error category: the process-wide category instance for CUDA runtime error codes.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/gpu/detail/cuda_category.hpp>
#if defined(KMX_AIO_FEATURE_CUDA)
    #ifndef PCH
        #include <kmx/aio/gpu/detail/cuda_error_category.hpp>
    #endif

namespace kmx::aio::gpu::detail
{
    const std::error_category& cuda_category() noexcept
    {
        static const cuda_error_category instance;
        return instance;
    }
}
#endif
