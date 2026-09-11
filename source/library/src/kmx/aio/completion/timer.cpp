/// @file src/kmx/aio/completion/timer.cpp
/// @brief Completion-model timer implementation over io_uring IORING_OP_TIMEOUT.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/completion/timer.hpp>

namespace kmx::aio::completion
{
    task_returning_expected_void_t timer::wait_ns(const std::uint64_t ns) noexcept(false)
    {
        const auto result = co_await exec_.async_timeout(ns);
        if (!result)
            co_return std::unexpected(result.error());

        co_return expected_void_t {};
    }

}
