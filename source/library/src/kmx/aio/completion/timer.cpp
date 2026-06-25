/// @file aio/completion/timer.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/completion/timer.hpp"

namespace kmx::aio::completion
{
    task<std::expected<void, std::error_code>> timer::wait_ns(const std::uint64_t ns) noexcept(false)
    {
        const auto result = co_await exec_->async_timeout(ns);
        if (!result)
            co_return std::unexpected(result.error());

        co_return std::expected<void, std::error_code> {};
    }

} // namespace kmx::aio::completion
