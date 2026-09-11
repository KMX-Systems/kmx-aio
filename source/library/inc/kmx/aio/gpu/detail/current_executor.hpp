/// @file inc/kmx/aio/gpu/detail/current_executor.hpp
/// @brief The GPU executor whose event loop runs on the calling thread.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_CUDA)

namespace kmx::aio::gpu
{
    class executor;
}

namespace kmx::aio::gpu::detail
{
    /// @brief The executor processing events on this thread, or null outside its event loop.
    /// @details Set by the executor around each resumption, so an event awaited from inside one of its
    ///          coroutines registers with that executor instead of blocking the thread.
    extern thread_local executor* current_executor;
}
#endif
