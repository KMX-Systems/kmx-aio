/// @file inc/kmx/aio/gpu/detail/task_queue.hpp
/// @brief A mutex-guarded queue of coroutines waiting for their first resumption on the GPU executor.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_CUDA)
    #ifndef PCH
        #include <kmx/aio/promise_base.hpp>

        #include <mutex>
        #include <utility>
        #include <vector>
    #endif

namespace kmx::aio::gpu::detail
{
    /// Task Queue (Pending Coroutines)

    class task_queue
    {
    public:
        void enqueue(coroutine_handle_t h) noexcept
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.push_back(h);
        }

        std::vector<coroutine_handle_t> drain() noexcept
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return std::exchange(pending_, {});
        }

    private:
        std::mutex mutex_;
        std::vector<coroutine_handle_t> pending_;
    };

}
#endif // KMX_AIO_FEATURE_CUDA
