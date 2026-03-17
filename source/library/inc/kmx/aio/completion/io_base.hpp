/// @file aio/completion/io_base.hpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <memory>

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
        io_base() = delete;

        explicit io_base(std::shared_ptr<executor> exec) noexcept: exec_(std::move(exec)) {}

        io_base(std::shared_ptr<executor> exec, file_descriptor&& fd) noexcept:
            exec_(std::move(exec)), fd_(std::move(fd))
        {
        }

        io_base(const io_base&) = delete;
        io_base& operator=(const io_base&) = delete;

        io_base(io_base&&) noexcept = default;
        io_base& operator=(io_base&&) noexcept = delete;

        [[nodiscard]] fd_t get_fd() const noexcept { return fd_.get(); }

    protected:
        std::shared_ptr<executor> exec_;
        file_descriptor fd_;
    };
} // namespace kmx::aio::completion
