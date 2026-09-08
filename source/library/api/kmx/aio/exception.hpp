/// @file aio/exception.hpp
/// @brief Exception types thrown by the KMX AIO library.
/// @details
/// Latency-critical paths report failure through @ref kmx::aio::error_code and `std::expected`, so the
/// types here are reserved for the two cases that cannot be expressed that way: a precondition the
/// caller violated, and a constructor that cannot leave its object in a usable state.
///
/// Every type below is project-defined, so a catch site can name exactly the library it is handling
/// rather than a standard type that any dependency might also throw. @ref kmx::aio::exception is a
/// marker base common to all of them, and each concrete type additionally derives from the standard
/// exception with the matching meaning, so `catch (const std::exception&)` and handlers written against
/// the standard hierarchy keep working unchanged.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <new>
    #include <stdexcept>
    #include <system_error>
#endif

namespace kmx::aio
{
    /// @brief Marker base shared by every exception the library throws.
    /// @details Carries no state and no message of its own. Catch it to handle anything originating in
    ///          this library without also catching the standard exceptions a dependency may throw.
    /// @note Concrete types derive from this and from a standard exception, so the standard hierarchy
    ///       stays usable at any catch site that prefers it.
    class exception
    {
    public:
        /// @brief Destroys the exception.
        virtual ~exception() noexcept;
    };

    /// @brief An unrecoverable runtime condition, detectable only while the operation runs.
    class runtime_error: public exception, public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    /// @brief A violated precondition, detectable by the caller before the call.
    class logic_error: public exception, public std::logic_error
    {
    public:
        using std::logic_error::logic_error;
    };

    /// @brief An argument outside the domain the callee accepts.
    class invalid_argument: public exception, public std::invalid_argument
    {
    public:
        using std::invalid_argument::invalid_argument;
    };

    /// @brief A failed operating system or third-party library call, carrying the code it reported.
    /// @note Inherits the `std::system_error` constructors, so a `std::error_code`, or an `int` together
    ///       with its `std::error_category`, is the way to build one.
    class system_error: public exception, public std::system_error
    {
    public:
        using std::system_error::system_error;
    };

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
