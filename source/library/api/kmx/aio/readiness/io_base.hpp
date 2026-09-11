/// @file api/kmx/aio/readiness/io_base.hpp
/// @brief Readiness-model socket base: owns the descriptor and unregisters it from the epoll executor on destruction.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_READINESS)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/file_descriptor.hpp>
        #include <kmx/aio/readiness/executor.hpp>

        #include <memory>
    #endif

namespace kmx::aio::readiness
{
    /// @brief Shared I/O base for protocol-specific socket wrappers.
    /// @details Owns a file descriptor and unregisters it from the executor on destruction
    ///          while the executor lifetime token is still valid.
    /// @note Not polymorphic: the destructor is protected and non-virtual, and there is no virtual I/O.
    ///       The completion model's counterpart says the same by declaring no destructor at all.
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
        io_base(executor& exec, file_descriptor&& fd) noexcept: exec_(exec), exec_lifetime_(exec.get_lifetime_token()), fd_(std::move(fd))
        {
        }

        /// @brief Non-copyable.
        io_base(const io_base&) = delete;
        /// @brief Non-copyable.
        io_base& operator=(const io_base&) = delete;

        /// @brief Move assignment is disabled because executor reference cannot be reseated.
        io_base& operator=(io_base&&) noexcept = delete;

        /// @brief Returns the owned file descriptor value.
        /// @return File descriptor or invalid fd value.
        [[nodiscard]] fd_t get_fd() const noexcept { return fd_.get(); }

    protected:
        /// @brief Move constructor.
        /// @note Protected for the same reason the destructor is: moving one of these constructs an
        ///       @c io_base that has to be destroyed again, which only a derived class can do. Left
        ///       public it would advertise an operation no caller outside the hierarchy can complete.
        io_base(io_base&&) noexcept = default;

        /// @brief Unregisters the descriptor from the executor if both are still valid.
        /// @note Protected and non-virtual: see the class note. A derived socket is destroyed as itself,
        ///       never through an @c io_base*, which is what this says in the type system.
        ~io_base() noexcept
        {
            if (fd_.is_valid() && !exec_lifetime_.expired())
                exec_.unregister_fd(fd_.get());
        }

        /// @brief Associated executor.
        executor& exec_;
        /// @brief Lifetime token to avoid touching executor after destruction.
        std::weak_ptr<void> exec_lifetime_;
        /// @brief Owned descriptor.
        file_descriptor fd_;
    };
}
#endif // KMX_AIO_FEATURE_READINESS
