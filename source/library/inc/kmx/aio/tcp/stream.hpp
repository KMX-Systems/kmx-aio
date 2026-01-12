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
    /// @brief Asynchronous TCP Stream.
    class stream: public base
    {
    public:
        using result_task = task<std::expected<std::size_t, std::error_code>>;

        stream(executor& exec, descriptor::file&& fd) noexcept: base(exec, std::move(fd)) {}
        ~stream() override = default;
        stream(stream&&) = default;
        stream& operator=(stream&&) = default;

        /// @brief Reads data into the buffer.
        /// @throws std::bad_alloc (Corountine frame allocation).
        result_task read(const std::span<char> buffer) noexcept(false);

        /// @brief Writes data from the buffer.
        /// @throws std::bad_alloc (Corountine frame allocation).
        result_task write(const std::span<const char> buffer) noexcept(false);

        /// @brief Writes all data, handling partial writes.
        /// @throws std::bad_alloc (Corountine frame allocation).
        task<std::expected<void, std::error_code>> write_all(std::vector<char> buffer) noexcept(false);
    };

} // namespace kmx::aio::tcp
