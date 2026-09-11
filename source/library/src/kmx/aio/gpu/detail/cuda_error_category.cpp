/// @file src/kmx/aio/gpu/detail/cuda_error_category.cpp
/// @brief The CUDA error category messages: the text of each CUDA runtime error code.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/gpu/detail/cuda_error_category.hpp>
#if defined(KMX_AIO_FEATURE_CUDA)
    #ifndef PCH
        #include <kmx/aio/gpu/basic_types.hpp>

        #include <string>
    #endif

namespace kmx::aio::gpu::detail
{
    std::string cuda_error_category::message(int ev) const
    {
        switch (static_cast<::cudaError_t>(ev))
        {
            case cudaSuccess:
                return "CUDA operation succeeded";
            case cudaErrorMemoryAllocation:
                return "CUDA out of memory";
            case cudaErrorInitializationError:
                return "CUDA initialization failed";
            case cudaErrorNotSupported:
                return "CUDA operation not supported";
            case cudaErrorNotReady:
                return "CUDA resource not ready";
            default:
                return "CUDA error code " + std::to_string(ev);
        }
    }
}
#endif
