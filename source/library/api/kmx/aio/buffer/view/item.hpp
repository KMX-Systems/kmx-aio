/// @file aio/buffer/view/item.hpp
/// @brief Non-owning view over a contiguous memory region, in a writable and a read-only flavor.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <span>
    #include <type_traits>
#endif

namespace kmx::aio::buffer::view
{
    /// @brief A buffer referencing contiguous memory, parameterized on the constness of that memory.
    /// @details Lightweight and non-owning, used by both readiness (epoll) and completion (io_uring)
    ///          models to describe I/O regions without copying. The two flavors differ only in whether
    ///          the memory may be written, which is what this template is parameterized on; they are
    ///          spelled writable and readable rather than used directly.
    /// @tparam Byte `std::byte` for a writable region, `const std::byte` for a read-only one.
    template <typename Byte>
        requires std::is_same_v<std::remove_const_t<Byte>, std::byte>
    struct item
    {
        /// @brief `T` for a writable view, `const T` for a read-only one.
        template <typename T>
        using element_t = std::conditional_t<std::is_const_v<Byte>, const T, T>;

        /// @brief The void type matching this view's constness.
        using void_t = element_t<void>;

        void_t* data {};     ///< Pointer to the start of the region.
        std::size_t size {}; ///< Number of bytes available at `data`.

        /// @brief Constructs an empty buffer.
        constexpr item() noexcept = default;

        /// @brief Constructs a buffer from a pointer and size.
        /// @param d Pointer to the memory.
        /// @param s Number of bytes.
        constexpr item(void_t* const d, const std::size_t s) noexcept: data(d), size(s) {}

        /// @brief Constructs a buffer from a std::span of bytes.
        /// @param span The span of bytes.
        constexpr explicit item(std::span<Byte> span) noexcept: data(span.data()), size(span.size()) {}

        /// @brief Constructs a buffer from a std::span of chars.
        /// @param span The span of chars.
        constexpr explicit item(std::span<element_t<char>> span) noexcept: data(span.data()), size(span.size()) {}

        /// @brief Implicit conversion from a writable buffer to a read-only one.
        /// @param other The writable buffer to view as read-only.
        /// @note A constructor template, never a copy constructor, so the implicit copy constructor of
        ///       the writable flavor stays declared. A plain constrained copy constructor would suppress
        ///       it and leave writable non-copyable.
        template <typename OtherByte>
            requires(std::is_const_v<Byte> && !std::is_const_v<OtherByte>)
        constexpr item(const item<OtherByte>& other) noexcept: data(other.data), size(other.size)
        {
        } // NOLINT

        /// @brief Returns a typed span view over this buffer.
        /// @tparam T The element type (must be trivially copyable).
        /// @return A span covering as many complete T elements as fit, const for a read-only view.
        template <typename T>
            requires std::is_trivially_copyable_v<T>
        [[nodiscard]] constexpr std::span<element_t<T>> as_span() const noexcept
        {
            return {static_cast<element_t<T>*>(data), size / sizeof(T)};
        }

        /// @brief Advances the buffer start by `n` bytes, reducing available size.
        /// @param n Number of bytes to consume.
        /// @return A new buffer starting after the consumed region.
        [[nodiscard]] constexpr item consume(const std::size_t n) const noexcept
        {
            const auto skip = (n < size) ? n : size;
            return {static_cast<Byte*>(data) + skip, size - skip};
        }
    };

    /// @brief A buffer referencing writable contiguous memory.
    /// @details Describes the target region of a read: the bytes an operation is allowed to fill.
    using writable = item<std::byte>;

    /// @brief A buffer referencing read-only contiguous memory.
    /// @details Describes the source region of a write. Converts implicitly from writable.
    using readable = item<const std::byte>;

} // namespace kmx::aio::buffer::view
