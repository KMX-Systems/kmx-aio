/// @file src/kmx/aio/gpu/detail/current_executor.cpp
/// @brief Storage for the GPU executor whose event loop runs on the calling thread.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/gpu/detail/current_executor.hpp>

#if defined(KMX_AIO_FEATURE_CUDA)

namespace kmx::aio::gpu::detail
{
    thread_local executor* current_executor {};
}
#endif
