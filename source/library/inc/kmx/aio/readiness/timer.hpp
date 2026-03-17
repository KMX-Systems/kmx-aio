/// @file aio/readiness/timer.hpp
/// @brief Readiness-model timer using timerfd + epoll for coroutine-based scheduling.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/descriptor/timer.hpp>
#endif

namespace kmx::aio::readiness
{
    /// @brief The readiness-model timer is the existing timerfd-based timer.
    /// @details Uses timerfd + epoll readiness notification to suspend a coroutine
    ///          until the timer fires. Exposes `co_await timer.wait(exec)`.
    using timer = kmx::aio::descriptor::timer;

} // namespace kmx::aio::readiness
