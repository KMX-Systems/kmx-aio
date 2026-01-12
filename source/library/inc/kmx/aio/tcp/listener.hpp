#pragma once
#ifndef PCH
    #include <expected>
    #include <span>
    #include <string>
    #include <string_view>
    #include <vector>

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/executor.hpp>
    #include <kmx/aio/task.hpp>
    #include <kmx/aio/tcp/base.hpp>
#endif

namespace kmx::aio::tcp
{
    /// @brief Asynchronous TCP listener.
    class listener: public base
    {
    public:
        /// @brief Creates a listener bound to the specified IP and port.
        /// @throws std::system_error If socket creation or bind fails.
        listener(executor& exec, const std::string_view ip, const std::uint16_t port) noexcept(false);
        ~listener() override = default;
        listener(listener&&) = default;
        listener& operator=(listener&&) = default;

        /// @brief Starts listening.
        /// @return Result or error code. Does not throw.
        result_t listen(const int backlog = 128) noexcept;

        /// @brief Asynchronously accepts a new connection.
        /// @return A task yielding the client file descriptor.
        /// @throws std::bad_alloc (Corountine frame allocation).
        task<std::expected<descriptor::file, std::error_code>> accept() noexcept(false);
    };

} // namespace kmx::aio::tcp
