/// @file aio/tcp/stream.hpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <expected>
    #include <span>

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/executor.hpp>
    #include <kmx/aio/io_base.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::tcp
{
    /// @brief Asynchronous TCP Stream.
    class stream: public io_base
    {
    public:
        /// @brief Task type used by TCP read/write operations.
        using result_task = task<std::expected<std::size_t, std::error_code>>;

        /// @brief Constructs stream from executor and connected socket descriptor.
        /// @param exec Executor used for wait_io suspension.
        /// @param fd   Connected socket descriptor owner.
        stream(executor& exec, descriptor::file&& fd) noexcept: io_base(exec, std::move(fd)) {}
        /// @brief Destroys the stream and unregisters descriptor if needed.
        ~stream() override = default;
        /// @brief Move constructor.
        stream(stream&&) noexcept = default;
        /// @brief Move assignment is disabled because base stores executor reference.
        stream& operator=(stream&&) noexcept = delete;

        /// @brief Reads data into the buffer.
        /// @throws std::bad_alloc (Corountine frame allocation).
        result_task read(const std::span<char> buffer) noexcept(false);

        /// @brief Writes data from the buffer.
        /// @throws std::bad_alloc (Corountine frame allocation).
        result_task write(const std::span<const char> buffer) noexcept(false);

        /// @brief Writes all data, handling partial writes.
        /// @throws std::bad_alloc (Corountine frame allocation).
        task<std::expected<void, std::error_code>> write_all(std::span<const char> buffer) noexcept(false);
    };

} // namespace kmx::aio::tcp
