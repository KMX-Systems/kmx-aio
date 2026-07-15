/// @file kmx/aio/modbus/mock_stream.hpp
/// @brief In-memory mock stream for unit-testing Modbus session framing.
/// @details
/// `mock_stream` satisfies the stream concept expected by
/// `kmx::aio::modbus::detail::session` function templates (`read`, `write_all`)
/// without performing any real I/O or requiring a running executor.
///
/// Usage pattern:
/// @code
///   mock_stream ms;
///   ms.push_read_bytes({0x00, 0x01, ...}); // pre-load server response
///   auto pdu = co_await detail::exchange(ms, adu_span, tid, unit_id);
///   CHECK(ms.written_bytes() == expected_adu);
/// @endcode
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#if defined(KMX_AIO_FEATURE_MODBUS)
    #ifndef PCH
        #include <cstdint>
        #include <deque>
        #include <expected>
        #include <span>
        #include <system_error>
        #include <vector>
    #endif

    #include <kmx/aio/task.hpp>

namespace kmx::aio::modbus::test
{
    /// @brief Simple coroutine-compatible in-memory stream for framing unit tests.
    class mock_stream
    {
    public:
        using result_task = task<std::expected<std::size_t, std::error_code>>;

        mock_stream() noexcept = default;

        // -----------------------------------------------------------------
        // Test setup API
        // -----------------------------------------------------------------

        /// @brief Pre-load bytes that will be returned from subsequent `read()` calls.
        /// @details Multiple calls enqueue additional byte sequences that are
        ///          consumed in the order they were pushed.
        void push_read_bytes(std::vector<std::uint8_t> bytes)
        {
            read_queue_.push_back(std::move(bytes));
        }

        /// @brief Return all bytes that have been written via `write_all()`.
        [[nodiscard]] const std::vector<std::uint8_t>& written_bytes() const noexcept
        {
            return written_;
        }

        /// @brief Clear written bytes and reset the read queue.
        void reset() noexcept
        {
            read_queue_.clear();
            written_.clear();
            read_offset_ = 0u;
        }

        // -----------------------------------------------------------------
        // Stream interface (satisfies detail::session requirements)
        // -----------------------------------------------------------------

        /// @brief Reads up to `buffer.size()` bytes from the pre-loaded queue.
        /// @details Never suspends; `await_ready()` on the underlying awaitable
        ///          is always `true`.
        result_task read(std::span<char> buffer) noexcept(false)
        {
            if (read_queue_.empty())
                co_return std::size_t {0u}; // EOF

            auto& front = read_queue_.front();
            const std::size_t available = front.size() - read_offset_;
            const std::size_t to_copy   = std::min(available, buffer.size());

            for (std::size_t i = 0u; i < to_copy; ++i)
                buffer[i] = static_cast<char>(front[read_offset_ + i]);

            read_offset_ += to_copy;
            if (read_offset_ >= front.size())
            {
                read_queue_.pop_front();
                read_offset_ = 0u;
            }

            co_return to_copy;
        }

        /// @brief Captures all bytes written; always reports full write success.
        task<std::expected<void, std::error_code>> write_all(
            std::span<const char> buffer) noexcept(false)
        {
            for (const char c: buffer)
                written_.push_back(static_cast<std::uint8_t>(c));
            co_return {};
        }

    private:
        std::deque<std::vector<std::uint8_t>> read_queue_;
        std::size_t read_offset_ = 0u;
        std::vector<std::uint8_t> written_;
    };

} // namespace kmx::aio::modbus::test
#endif // KMX_AIO_FEATURE_MODBUS
