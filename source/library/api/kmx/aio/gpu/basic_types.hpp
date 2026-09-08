/// @file aio/gpu/basic_types.hpp
/// @brief Opaque GPU handle types shared by the GPU stream, event and executor classes.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_CUDA)

    #include <cuda_runtime.h>

namespace kmx::aio::gpu
{
    /// @brief Opaque handle to a GPU stream (CUDA stream or mock).
    using stream_handle = void*;

    /// @brief Opaque handle to a GPU event (CUDA event or mock).
    using event_handle = void*;

} // namespace kmx::aio::gpu
#endif // KMX_AIO_FEATURE_CUDA
