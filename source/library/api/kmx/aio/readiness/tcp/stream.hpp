/// @file aio/readiness/tcp/stream.hpp
/// @brief Readiness-model TCP stream using epoll-based async I/O.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <expected>
    #include <span>

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/readiness/io_base.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::readiness::tcp
{
    /// @brief Asynchronous TCP Stream.
    class stream: public io_base
    {
    public:
        stream(executor& exec, file_descriptor&& fd) noexcept: io_base(exec, std::move(fd)) {}
        ~stream() override = default;
        stream(stream&&) noexcept = default;
        stream& operator=(stream&&) noexcept = delete;

        task_returning_expected_size_t read(std::span<char> buffer) noexcept(false);
        task_returning_expected_size_t write(std::span<const char> buffer) noexcept(false);
        task_returning_expected_void_t write_all(std::span<const char> buffer) noexcept(false);
    };

} // namespace kmx::aio::readiness::tcp
