/// @file aio/buffer.hpp
/// @brief Unified memory buffer views for zero-copy I/O across readiness and completion models.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstring>
    #include <span>
    #include <type_traits>
#endif

namespace kmx::aio
{
    /// @brief A mutable buffer referencing contiguous memory.
    /// @details Lightweight, non-owning view used by both readiness (epoll) and completion (io_uring)
    ///          models to describe I/O target regions without copying.
    struct mutable_buffer
    {
        void* data {};               ///< Pointer to the start of the writable region.
        std::size_t size {};          ///< Number of bytes available at `data`.

        /// @brief Constructs an empty buffer.
        constexpr mutable_buffer() noexcept = default;

        /// @brief Constructs a mutable buffer from a pointer and size.
        /// @param d Pointer to the writable memory.
        /// @param s Number of bytes.
        constexpr mutable_buffer(void* const d, const std::size_t s) noexcept: data(d), size(s) {}

        /// @brief Constructs a mutable buffer from a std::span of bytes.
        /// @param span The span of writable bytes.
        constexpr explicit mutable_buffer(std::span<std::byte> span) noexcept: data(span.data()), size(span.size()) {}

        /// @brief Constructs a mutable buffer from a std::span of chars.
        /// @param span The span of writable chars.
        constexpr explicit mutable_buffer(std::span<char> span) noexcept: data(span.data()), size(span.size()) {}

        /// @brief Returns a typed span view over this buffer.
        /// @tparam T The element type (must be trivially copyable).
        /// @return A span covering as many complete T elements as fit.
        template <typename T>
            requires std::is_trivially_copyable_v<T>
        [[nodiscard]] constexpr std::span<T> as_span() noexcept
        {
            return {static_cast<T*>(data), size / sizeof(T)};
        }

        /// @brief Advances the buffer start by `n` bytes, reducing available size.
        /// @param n Number of bytes to consume.
        /// @return A new mutable_buffer starting after the consumed region.
        [[nodiscard]] constexpr mutable_buffer consume(const std::size_t n) const noexcept
        {
            const auto skip = (n < size) ? n : size;
            return {static_cast<std::byte*>(data) + skip, size - skip};
        }
    };

    /// @brief An immutable buffer referencing contiguous memory.
    /// @details Lightweight, non-owning view for outbound I/O data.
    struct const_buffer
    {
        const void* data {};          ///< Pointer to the start of the readable region.
        std::size_t size {};          ///< Number of bytes available at `data`.

        /// @brief Constructs an empty buffer.
        constexpr const_buffer() noexcept = default;

        /// @brief Constructs a const buffer from a pointer and size.
        /// @param d Pointer to the readable memory.
        /// @param s Number of bytes.
        constexpr const_buffer(const void* const d, const std::size_t s) noexcept: data(d), size(s) {}

        /// @brief Constructs a const buffer from a std::span of bytes.
        /// @param span The span of readable bytes.
        constexpr explicit const_buffer(std::span<const std::byte> span) noexcept: data(span.data()), size(span.size()) {}

        /// @brief Constructs a const buffer from a std::span of chars.
        /// @param span The span of readable chars.
        constexpr explicit const_buffer(std::span<const char> span) noexcept: data(span.data()), size(span.size()) {}

        /// @brief Implicit conversion from mutable_buffer.
        /// @param buf The mutable buffer to view as const.
        constexpr const_buffer(const mutable_buffer& buf) noexcept: data(buf.data), size(buf.size) {} // NOLINT

        /// @brief Returns a typed span view over this buffer.
        /// @tparam T The element type (must be trivially copyable).
        /// @return A const span covering as many complete T elements as fit.
        template <typename T>
            requires std::is_trivially_copyable_v<T>
        [[nodiscard]] constexpr std::span<const T> as_span() const noexcept
        {
            return {static_cast<const T*>(data), size / sizeof(T)};
        }

        /// @brief Advances the buffer start by `n` bytes, reducing available size.
        /// @param n Number of bytes to consume.
        /// @return A new const_buffer starting after the consumed region.
        [[nodiscard]] constexpr const_buffer consume(const std::size_t n) const noexcept
        {
            const auto skip = (n < size) ? n : size;
            return {static_cast<const std::byte*>(data) + skip, size - skip};
        }
    };

} // namespace kmx::aio
