/// @file aio/io_base.hpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <memory>

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/descriptor/file.hpp>
    #include <kmx/aio/executor.hpp>
#endif

namespace kmx::aio
{
    /// @brief Shared I/O base for protocol-specific socket wrappers.
    /// @details Owns a file descriptor and unregisters it from the executor on destruction
    ///          while the executor lifetime token is still valid.
    class io_base
    {
    public:
        /// @brief Default construction is disabled.
        io_base() = delete;

        /// @brief Constructs the base with executor only.
        /// @param exec Executor used for event registration and unregistration.
        explicit io_base(executor& exec) noexcept: exec_(exec), exec_lifetime_(exec.get_lifetime_token()) {}

        /// @brief Constructs the base with executor and an owned file descriptor.
        /// @param exec Executor used for event registration and unregistration.
        /// @param fd   File descriptor owner moved into this object.
        io_base(executor& exec, descriptor::file&& fd) noexcept: exec_(exec), exec_lifetime_(exec.get_lifetime_token()), fd_(std::move(fd))
        {
        }

        /// @brief Non-copyable.
        io_base(const io_base&) = delete;
        /// @brief Non-copyable.
        io_base& operator=(const io_base&) = delete;

        /// @brief Virtual destructor.
        /// @details Unregisters the descriptor from executor if both descriptor and executor are still valid.
        virtual ~io_base() noexcept
        {
            if (fd_.is_valid() && !exec_lifetime_.expired())
                exec_.unregister_fd(fd_.get());
        }

        /// @brief Move constructor.
        io_base(io_base&&) noexcept = default;
        /// @brief Move assignment is disabled because executor reference cannot be reseated.
        io_base& operator=(io_base&&) noexcept = delete;

        /// @brief Returns the owned file descriptor value.
        /// @return File descriptor or invalid fd value.
        [[nodiscard]] fd_t get_fd() const noexcept { return fd_.get(); }

    protected:
        /// @brief Associated executor.
        executor& exec_;
        /// @brief Lifetime token to avoid touching executor after destruction.
        std::weak_ptr<void> exec_lifetime_;
        /// @brief Owned descriptor.
        descriptor::file fd_;
    };
} // namespace kmx::aio
