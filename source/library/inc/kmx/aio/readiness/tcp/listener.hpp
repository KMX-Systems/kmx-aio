/// @file aio/readiness/tcp/listener.hpp
/// @brief Readiness-model TCP listener using epoll-based async accept.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <expected>

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/readiness/io_base.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::readiness::tcp
{
    /// @brief Asynchronous TCP listener.
    class listener: public io_base
    {
    public:
        using result_t = std::expected<void, std::error_code>;

        listener(executor& exec, ip_address_t ip, port_t port) noexcept(false);
        ~listener() override = default;
        listener(listener&&) noexcept = default;
        listener& operator=(listener&&) noexcept = delete;

        result_t listen(const int backlog = 128) noexcept;
        task<std::expected<file_descriptor, std::error_code>> accept() noexcept(false);
    };

} // namespace kmx::aio::readiness::tcp
