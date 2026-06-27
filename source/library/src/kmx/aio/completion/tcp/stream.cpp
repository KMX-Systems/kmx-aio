/// @file aio/completion/tcp/stream.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/completion/tcp/stream.hpp"

namespace kmx::aio::completion::tcp
{
    stream::result_task stream::read(std::span<char> buffer) noexcept(false)
    {
        co_return co_await exec_->async_read(fd_.get(), buffer);
    }

    stream::result_task stream::write(std::span<const char> buffer) noexcept(false)
    {
        co_return co_await exec_->async_write(fd_.get(), buffer);
    }

    task<std::expected<void, std::error_code>> stream::write_all(std::span<const char> buffer) noexcept(false)
    {
        for (std::size_t offset {}; offset < buffer.size();)
        {
            const std::span<const char> chunk {buffer.data() + offset, buffer.size() - offset};
            const auto res = co_await write(chunk);
            if (!res)
                co_return std::unexpected(res.error());

            if (*res == 0u)
                co_return std::unexpected(std::make_error_code(std::errc::broken_pipe));

            offset += *res;
        }

        co_return std::expected<void, std::error_code> {};
    }

    stream::result_task stream::read_fixed(std::span<char> buffer, const int buf_index) noexcept(false)
    {
        co_return co_await exec_->async_read_fixed(fd_.get(), buffer, 0, buf_index);
    }

    stream::result_task stream::write_fixed(std::span<const char> buffer, const int buf_index) noexcept(false)
    {
        co_return co_await exec_->async_write_fixed(fd_.get(), buffer, 0, buf_index);
    }

    task<std::expected<void, std::error_code>> stream::write_all_fixed(std::span<const char> buffer, const int buf_index) noexcept(false)
    {
        for (std::size_t offset {}; offset < buffer.size();)
        {
            const std::span<const char> chunk {buffer.data() + offset, buffer.size() - offset};
            const auto res = co_await write_fixed(chunk, buf_index);
            if (!res)
                co_return std::unexpected(res.error());

            if (*res == 0u)
                co_return std::unexpected(std::make_error_code(std::errc::broken_pipe));

            offset += *res;
        }

        co_return std::expected<void, std::error_code> {};
    }

} // namespace kmx::aio::completion::tcp
