/// @file aio/async_mutex.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/async_mutex.hpp>
#ifndef PCH
    #include <source_location>

    #include <kmx/logger.hpp>
#endif

namespace kmx::aio
{
    bool async_mutex::try_lock() noexcept
    {
        const std::lock_guard lock(state_mutex_);
        if (held_)
            return false;

        held_ = true;

        // Whoever called this is not identified as a coroutine, so nothing is recorded as the holder.
        // Leaving the field empty is the point: a stale handle here would be compared against a later
        // waiter and could name the wrong coroutine as deadlocked.
        holder_ = {};
        return true;
    }

    bool async_mutex::acquire_or_enqueue(const coroutine_handle_t handle) noexcept(false)
    {
        const std::lock_guard lock(state_mutex_);

        if (!held_)
        {
            held_ = true;
            holder_ = handle;
            return false;
        }

        // A coroutine awaiting the mutex it already holds joins a queue only it can drain. Nothing here
        // can rescue it - resuming it would hand out ownership twice, and refusing to queue it would
        // resume it without ownership - but the hang it is about to enter is otherwise indistinguishable
        // from a slow peer, so it is named while there is still something to name it with.
        if (handle == holder_)
            logger::log(logger::level::error, std::source_location::current(),
                        "async_mutex: coroutine awaiting a lock it already holds; it will never resume");

        waiters_.push_back(handle);
        return true;
    }

    void async_mutex::unlock() noexcept
    {
        coroutine_handle_t next {};
        {
            const std::lock_guard lock(state_mutex_);
            if (waiters_.empty())
                held_ = false;
            else
            {
                // held_ stays true: ownership passes straight to this waiter rather than being dropped
                // and re-taken, so nothing can slip in front of a coroutine that has already queued.
                next = waiters_.front();
                waiters_.pop_front();
            }

            holder_ = next;
        }

        if (next)
            next.resume();
    }

} // namespace kmx::aio
