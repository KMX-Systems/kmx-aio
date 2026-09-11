/// @file src/kmx/aio/gpu/event.cpp
/// @brief GPU event: CUDA event lifetime, readiness queries and the coroutine awaiter.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/gpu/event.hpp>
#ifndef PCH
    #include <kmx/aio/gpu/detail/cuda_category.hpp>
    #include <kmx/aio/gpu/detail/current_executor.hpp>
    #include <kmx/aio/gpu/executor.hpp>
    #include <kmx/aio/invalid_argument.hpp>
    #include <kmx/aio/system_error.hpp>

    #include <system_error>
    #include <thread>
    #include <utility>
#endif

namespace kmx::aio::gpu
{
    event::event() noexcept(false)
    {
#if defined(KMX_AIO_FEATURE_CUDA)
        ::cudaEvent_t e = nullptr;
        const auto ret = ::cudaEventCreate(&e);
        if (ret != cudaSuccess)
            throw system_error(static_cast<int>(ret), std::generic_category(), "cudaEventCreate failed");
        handle_ = e;
#else
        handle_ = reinterpret_cast<void*>(1); // Mock handle
#endif
    }

    event::~event() noexcept
    {
        destroy();
    }

    event::event(event&& other) noexcept: handle_(std::exchange(other.handle_, nullptr))
    {
    }

    event& event::operator=(event&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }

        return *this;
    }

    event::awaiter event::operator co_await() noexcept
    {
        return awaiter {*this};
    }

    bool event::is_ready() const noexcept(false)
    {
#if defined(KMX_AIO_FEATURE_CUDA)
        if (handle_ == nullptr)
            throw system_error(static_cast<int>(std::errc::invalid_argument), std::generic_category(), "event handle is null");

        const auto ret = ::cudaEventQuery(static_cast<::cudaEvent_t>(handle_));
        if (ret == cudaSuccess)
            return true;
        if (ret == cudaErrorNotReady)
            return false;

        throw system_error(static_cast<int>(ret), detail::cuda_category(), "cudaEventQuery failed");
#else
        return true; // Mock: always ready
#endif
    }

    bool event::awaiter::await_ready() const noexcept
    {
        // Poll without throwing.
#if defined(KMX_AIO_FEATURE_CUDA)
        const auto ret = ::cudaEventQuery(static_cast<::cudaEvent_t>(event_.handle_));
        return ret == cudaSuccess;
#else
        return true; // Mock
#endif
    }

    void event::awaiter::await_suspend(coroutine_handle_t h) noexcept
    {
        auto* const exec = detail::current_executor;
        if (exec == nullptr)
        {
#if defined(KMX_AIO_FEATURE_CUDA)
            while (true)
            {
                const auto ret = ::cudaEventQuery(static_cast<::cudaEvent_t>(event_.handle_));
                if (ret == cudaSuccess)
                    break;

                if (ret != cudaErrorNotReady)
                    break;

                std::this_thread::yield();
            }
#endif

            h.resume();
            return;
        }

        exec->register_waiting_coroutine(event_.handle_, h);
    }

    void event::destroy() noexcept
    {
        if (handle_ == nullptr)
            return;

#if defined(KMX_AIO_FEATURE_CUDA)
        ::cudaEventDestroy(static_cast<::cudaEvent_t>(handle_));
#endif

        handle_ = nullptr;
    }
}
