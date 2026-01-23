#pragma once
#ifndef PCH
    #include <expected>
    #include <memory>
    #include <system_error>

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/descriptor/file.hpp>
    #include <kmx/aio/executor.hpp>
#endif

namespace kmx::aio::tcp
{
    /// @brief Base class for TCP components.
    class base
    {
    public:
        using result_t = std::expected<void, std::error_code>;

        base(executor& exec) noexcept: exec_(exec), exec_lifetime_(exec.get_lifetime_token()) {}
        base(executor& exec, descriptor::file&& fd) noexcept: exec_(exec), exec_lifetime_(exec.get_lifetime_token()), fd_(std::move(fd)) {}

        virtual ~base() noexcept
        {
            if (fd_.is_valid() && !exec_lifetime_.expired())
                exec_.unregister_fd(fd_.get());
        }

        base(base&&) = default;
        base& operator=(base&&) = default;

        [[nodiscard]] fd_t get_fd() const noexcept { return fd_.get(); }

    protected:
        executor& exec_;
        std::weak_ptr<void> exec_lifetime_;
        descriptor::file fd_;
    };
} // namespace kmx::aio::tcp
