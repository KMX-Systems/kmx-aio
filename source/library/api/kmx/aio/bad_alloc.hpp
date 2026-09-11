/// @file api/kmx/aio/bad_alloc.hpp
/// @brief Library exception for a failed allocation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/exception.hpp>

    #include <new>
#endif

namespace kmx::aio
{
    /// @brief A failed allocation, including one made on the library's behalf by a dependency.
    /// @note The message is a borrowed string literal rather than an owned string: allocating to
    ///       describe a failed allocation is how a bad_alloc handler turns into a second failure.
    class bad_alloc: public exception, public std::bad_alloc
    {
    public:
        /// @brief Constructs the exception with a generic message.
        bad_alloc() noexcept = default;

        /// @brief Constructs the exception with a specific message.
        /// @param message A string with static storage duration, stored by pointer and not copied.
        explicit bad_alloc(const char* const message) noexcept: message_(message) {}

        /// @brief Returns the message the exception was constructed with.
        /// @return A pointer to the message, valid for as long as the string passed in was.
        [[nodiscard]] const char* what() const noexcept override { return message_; }

    private:
        const char* message_ {"kmx::aio::bad_alloc"};
    };
}
