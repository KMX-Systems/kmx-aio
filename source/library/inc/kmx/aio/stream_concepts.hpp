/// @file aio/stream_concepts.hpp
/// @brief Polymorphic stream interfaces for backend-agnostic I/O consumption.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <concepts>
    #include <cstddef>
    #include <expected>
    #include <span>

    #include <kmx/aio/error_code.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio
{
    /// @brief Concept constraining types that provide asynchronous byte reading.
    /// @details Any transport or security overlay (TCP stream, TLS stream, mock socket)
    ///          that models this concept can be consumed by higher-level parsers
    ///          (HTTP, WebSocket, etc.) without knowledge of the underlying backend.
    /// @tparam T The type to check against the stream_reader concept.
    template <typename T>
    concept stream_reader = requires(T& t, std::span<char> buf) {
        { t.read(buf) } -> std::same_as<task<std::expected<std::size_t, std::error_code>>>;
    };

    /// @brief Concept constraining types that provide asynchronous byte writing.
    /// @details Symmetric counterpart to stream_reader. Any transport that satisfies
    ///          this concept can be used as a sink by higher-level protocol encoders.
    /// @tparam T The type to check against the stream_writer concept.
    template <typename T>
    concept stream_writer = requires(T& t, std::span<const char> buf) {
        { t.write(buf) } -> std::same_as<task<std::expected<std::size_t, std::error_code>>>;
    };

    /// @brief Concept constraining types that provide both reading and writing.
    /// @tparam T The type to check against the stream concept.
    template <typename T>
    concept stream = stream_reader<T> && stream_writer<T>;

} // namespace kmx::aio
