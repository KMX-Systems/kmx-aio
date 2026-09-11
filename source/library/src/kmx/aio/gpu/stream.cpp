/// @file src/kmx/aio/gpu/stream.cpp
/// @brief GPU stream: creation, synchronization, event recording and destruction of a CUDA stream.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/gpu/stream.hpp>
#ifndef PCH
    #include <kmx/aio/gpu/detail/cuda_category.hpp>
    #include <kmx/aio/invalid_argument.hpp>
    #include <kmx/aio/system_error.hpp>

    #include <system_error>
    #include <utility>
#endif

namespace kmx::aio::gpu
{
    stream::stream() noexcept(false)
    {
#if defined(KMX_AIO_FEATURE_CUDA)
        ::cudaStream_t s = nullptr;
        const auto ret = ::cudaStreamCreate(&s);
        if (ret != cudaSuccess)
            throw system_error(static_cast<int>(ret), detail::cuda_category(), "cudaStreamCreate failed");
        handle_ = s;
#else
        handle_ = reinterpret_cast<void*>(0xDEADBEEF); // Mock handle (distinctive pattern)
#endif
    }

    stream::~stream() noexcept
    {
        destroy();
    }

    stream::stream(stream&& other) noexcept: handle_(std::exchange(other.handle_, nullptr))
    {
    }

    stream& stream::operator=(stream&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }

        return *this;
    }

    void stream::synchronize() noexcept(false)
    {
#if defined(KMX_AIO_FEATURE_CUDA)
        if (handle_ == nullptr)
            throw system_error(static_cast<int>(std::errc::invalid_argument), std::generic_category(), "stream handle is null");

        const auto ret = ::cudaStreamSynchronize(static_cast<::cudaStream_t>(handle_));
        if (ret != cudaSuccess)
            throw system_error(static_cast<int>(ret), detail::cuda_category(), "cudaStreamSynchronize failed");
#endif
    }

    event stream::create_event() noexcept(false)
    {
        event e;
#if defined(KMX_AIO_FEATURE_CUDA)
        if (handle_ == nullptr)
            throw system_error(static_cast<int>(std::errc::invalid_argument), std::generic_category(), "stream handle is null");
        const auto ret_record = ::cudaEventRecord(static_cast<::cudaEvent_t>(e.handle_), static_cast<::cudaStream_t>(handle_));
        if (ret_record != cudaSuccess)
            throw system_error(static_cast<int>(ret_record), detail::cuda_category(), "cudaEventRecord failed");
#else
        e.handle_ = reinterpret_cast<void*>(0xCAFEBABE); // Mock handle (distinctive pattern)
#endif
        return e;
    }

    void stream::destroy() noexcept
    {
        if (handle_ == nullptr)
            return;

#if defined(KMX_AIO_FEATURE_CUDA)
        ::cudaStreamDestroy(static_cast<::cudaStream_t>(handle_));

#endif

        handle_ = nullptr;
    }
}
