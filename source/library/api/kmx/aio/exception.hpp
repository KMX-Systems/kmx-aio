/// @file api/kmx/aio/exception.hpp
/// @brief Marker base of the exception types thrown by the KMX AIO library.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// Latency-critical paths report failure through @ref kmx::aio::error_code and `std::expected`, so the
/// library's exception types are reserved for the two cases that cannot be expressed that way: a precondition
/// the caller violated, and a constructor that cannot leave its object in a usable state.
///
/// Every one of them is project-defined, so a catch site can name exactly the library it is handling
/// rather than a standard type that any dependency might also throw. @ref kmx::aio::exception is a
/// marker base common to all of them, and each concrete type additionally derives from the standard
/// exception with the matching meaning, so `catch (const std::exception&)` and handlers written against
/// the standard hierarchy keep working unchanged.
#pragma once

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
}
