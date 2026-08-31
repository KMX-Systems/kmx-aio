/// @file aio/async_mutex.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/async_mutex.hpp>

namespace kmx::aio
{
    bool async_mutex::try_lock() noexcept
    {
        const std::lock_guard lock(state_mutex_);
        if (held_)
            return false;

        held_ = true;
        return true;
    }

    bool async_mutex::enqueue(const std::coroutine_handle<> handle) noexcept(false)
    {
        const std::lock_guard lock(state_mutex_);

        // await_ready() found the mutex held, but the holder may have released it between that check
        // and this one. Taking it here rather than queueing saves the coroutine a suspension it would
        // be resumed from immediately.
        if (!held_)
        {
            held_ = true;
            return false;
        }

        waiters_.push_back(handle);
        return true;
    }

    void async_mutex::unlock() noexcept
    {
        std::coroutine_handle<> next {};
        {
            const std::lock_guard lock(state_mutex_);
            if (waiters_.empty())
            {
                held_ = false;
            }
            else
            {
                // held_ stays true: ownership passes straight to this waiter rather than being dropped
                // and re-taken, so nothing can slip in front of a coroutine that has already queued.
                next = waiters_.front();
                waiters_.pop_front();
            }
        }

        if (next)
            next.resume();
    }

} // namespace kmx::aio
