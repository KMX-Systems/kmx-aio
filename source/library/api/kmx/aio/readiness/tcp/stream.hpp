/// @file aio/readiness/tcp/stream.hpp
/// @brief Readiness-model TCP stream using epoll-based async I/O.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <span>

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
        /// @brief Adopts a connected socket.
        /// @param exec The executor the descriptor is registered with.
        /// @param fd   The connected socket descriptor.
        stream(executor& exec, file_descriptor&& fd) noexcept: io_base(exec, std::move(fd)) {}
        /// @brief Unregisters the descriptor and closes the socket.
        ~stream() override = default;
        /// @brief Move constructor — transfers ownership of the descriptor.
        stream(stream&&) noexcept = default;
        /// @brief Move assignment is disabled: the executor reference cannot be reseated.
        stream& operator=(stream&&) noexcept = delete;

        /// @brief Suspends until the socket is readable, then reads into the buffer.
        /// @param buffer Destination buffer.
        /// @return A task yielding the number of bytes read, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        task_returning_expected_size_t read(span_char_t buffer) noexcept(false);
        /// @brief Suspends until the socket is writable, then writes what it accepts.
        /// @param buffer Source buffer.
        /// @return A task yielding the number of bytes written, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        task_returning_expected_size_t write(cspan_char_t buffer) noexcept(false);
        /// @brief Writes the whole buffer, reissuing writes until nothing is left.
        /// @param buffer Source buffer.
        /// @return A task yielding success once every byte was written, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        task_returning_expected_void_t write_all(cspan_char_t buffer) noexcept(false);
    };

} // namespace kmx::aio::readiness::tcp
