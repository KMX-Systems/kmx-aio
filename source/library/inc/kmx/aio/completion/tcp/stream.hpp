/// @file aio/completion/tcp/stream.hpp
/// @brief Completion-model TCP stream using io_uring for async read/write.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <expected>
    #include <memory>
    #include <span>
    #include <system_error>

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/descriptor/file.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::completion::tcp
{
    /// @brief Asynchronous TCP stream backed by io_uring completion I/O.
    /// @details In the completion model, the kernel performs the actual read/write
    ///          and signals the coroutine when the buffer has been filled/drained.
    ///          This eliminates the readiness-then-syscall round-trip of epoll.
    class stream
    {
    public:
        /// @brief Task type returned by read/write operations.
        using result_task = task<std::expected<std::size_t, std::error_code>>;

        /// @brief Constructs a stream from an executor and an owned socket descriptor.
        /// @param exec The completion executor providing io_uring operations.
        /// @param fd   Connected socket descriptor (ownership transferred).
        stream(std::shared_ptr<executor> exec, descriptor::file&& fd) noexcept:
            exec_(std::move(exec)), fd_(std::move(fd))
        {
        }

        /// @brief Destructor. Closes the file descriptor.
        ~stream() noexcept = default;

        /// @brief Non-copyable.
        stream(const stream&) = delete;
        /// @brief Non-copyable.
        stream& operator=(const stream&) = delete;

        /// @brief Move constructor.
        stream(stream&&) noexcept = default;
        /// @brief Move assignment is disabled.
        stream& operator=(stream&&) noexcept = delete;

        /// @brief Asynchronously reads data into the buffer via io_uring.
        /// @param buffer Destination buffer; the kernel writes directly here.
        /// @return A task yielding the number of bytes read, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] result_task read(std::span<char> buffer) noexcept(false);

        /// @brief Asynchronously writes data from the buffer via io_uring.
        /// @param buffer Source buffer.
        /// @return A task yielding the number of bytes written, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] result_task write(std::span<const char> buffer) noexcept(false);

        [[nodiscard]] task<std::expected<void, std::error_code>> write_all(std::span<const char> buffer) noexcept(false);

        /// @brief Asynchronously reads data into a pre-registered buffer via io_uring.
        /// @param buffer Destination buffer; must be part of the registered iovec.
        /// @param buf_index Index into the registered iovec array.
        /// @return A task yielding the number of bytes read, or an error.
        [[nodiscard]] result_task read_fixed(std::span<char> buffer, const int buf_index) noexcept(false);

        /// @brief Asynchronously writes data from a pre-registered buffer via io_uring.
        /// @param buffer Source buffer; must be part of the registered iovec.
        /// @param buf_index Index into the registered iovec array.
        /// @return A task yielding the number of bytes written, or an error.
        [[nodiscard]] result_task write_fixed(std::span<const char> buffer, const int buf_index) noexcept(false);

        /// @brief Writes all data from a pre-registered buffer, handling partial writes.
        /// @param buffer Source buffer; must be part of the registered iovec.
        /// @param buf_index Index into the registered iovec array.
        /// @return A task yielding success or an error.
        [[nodiscard]] task<std::expected<void, std::error_code>> write_all_fixed(std::span<const char> buffer, const int buf_index) noexcept(false);

        /// @brief Returns the underlying file descriptor value.
        [[nodiscard]] fd_t get_fd() const noexcept { return fd_.get(); }

    private:
        std::shared_ptr<executor> exec_;
        descriptor::file fd_;
    };

} // namespace kmx::aio::completion::tcp
