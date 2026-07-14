/// @file aio/completion/io_base.hpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/file_descriptor.hpp>
#endif

namespace kmx::aio::completion
{
    /// @brief Shared I/O base for completion-model socket wrappers.
    class io_base
    {
    public:
        /// @brief Default construction is not allowed.
        io_base() = delete;

        /// @brief Creates a base wrapper that only stores the executor.
        /// @param exec Completion executor used by derived types. Must outlive this wrapper.
        explicit io_base(executor& exec) noexcept: exec_(exec) {}

        /// @brief Creates a base wrapper with an executor and owned file descriptor.
        /// @param exec Completion executor used by derived types. Must outlive this wrapper.
        /// @param fd Owned file descriptor to associate with the wrapper.
        io_base(executor& exec, file_descriptor&& fd) noexcept: exec_(exec), fd_(std::move(fd)) {}

        /// @brief Non-copyable.
        io_base(const io_base&) = delete;
        /// @brief Non-copyable.
        io_base& operator=(const io_base&) = delete;

        /// @brief Movable base wrapper.
        io_base(io_base&&) noexcept = default;
        /// @brief Non-movable assignment.
        io_base& operator=(io_base&&) noexcept = delete;

        /// @brief Returns the wrapped file descriptor.
        /// @return The underlying descriptor value.
        [[nodiscard]] fd_t get_fd() const noexcept { return fd_.get(); }

    protected:
        /// @brief Completion executor used by derived wrappers. Non-owning; the caller
        ///        must guarantee the executor outlives every io_base built on top of it.
        executor& exec_;
        /// @brief Owned descriptor managed by the wrapper.
        file_descriptor fd_;
    };
} // namespace kmx::aio::completion
