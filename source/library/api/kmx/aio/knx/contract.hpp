/// @file api/kmx/aio/knx/contract.hpp
/// @brief Lightweight contract support for protocol invariants.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// These macros state invariants the surrounding code has already established — a length that a preceding
/// `if` has just checked, an index the caller cannot have got wrong. They are hardening and documentation,
/// not input validation: everything a peer can influence is validated by the codec and reported as a
/// @ref kmx::aio::knx::error, never as a terminating check.
///
/// Because they restate what is already guaranteed, they compile out under `NDEBUG` by default. Define
/// `KMX_AIO_CONTRACTS_ENABLED` to `1` or `0` to force them on or off regardless.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <cstdio>
        #include <cstdlib>
        #include <exception>
        #include <source_location>
        #include <string_view>
    #endif

namespace kmx::aio::knx
{
    namespace detail
    {
        /// @brief Reports a violated invariant and ends the process.
        /// @param condition The source text of the condition that failed.
        /// @param location Where the check sits.
        [[noreturn]] inline void contract_violation(const char* condition,
                                                    const std::source_location location = std::source_location::current()) noexcept
        {
            std::fputs("KNX contract violation: ", stderr);
            if (condition != nullptr)
            {
                std::fputs(condition, stderr);
                std::fputc(' ', stderr);
            }

            std::fputs("at ", stderr);
            std::fputs(location.file_name(), stderr);
            std::fputc(':', stderr);
            std::fprintf(stderr, "%d", location.line());
            std::fputc('\n', stderr);
            std::terminate();
        }
    }

    /// @brief Aborts on a violated invariant, for the checked build.
    /// @param condition The invariant that must hold.
    /// @param text The source text of the condition, for the diagnostic.
    inline void check_contract(const bool condition, const char* text,
                               const std::source_location location = std::source_location::current()) noexcept
    {
        if (!condition)
            detail::contract_violation(text, location);
    }

    /// @brief Returns a value only if an invariant holds, aborting otherwise.
    /// @tparam T The value type.
    /// @param condition The invariant that must hold.
    /// @param value The value to return when it does.
    /// @param message A diagnostic naming the invariant.
    /// @param location Where the check sits.
    /// @return @p value.
    template <typename T>
    [[nodiscard]] constexpr T require(const bool condition, T value, const char* message = nullptr,
                                      const std::source_location location = std::source_location::current()) noexcept
    {
        if (!condition)
            detail::contract_violation(message, location);

        return value;
    }
}

    // Whether the checks are compiled in at all. They restate invariants the code has already established, so
    // a release build has nothing to gain from them.
    //
    // The C++26 contracts syntax is deliberately not detected here. `__cpp_contracts` has named two entirely
    // different, mutually incompatible designs over the years - the withdrawn `[[expects: c]]` attribute and
    // the current `pre(c)` / `contract_assert(c)` - so a compiler that defines it says nothing about which one
    // it will accept, and guessing wrong does not degrade gracefully: it stops the build. A translation unit
    // that wants the native facility can define KMX_AIO_EXPECTS and KMX_AIO_ENSURES itself before this header.
    #if !defined(KMX_AIO_CONTRACTS_ENABLED)
        #if defined(NDEBUG)
            #define KMX_AIO_CONTRACTS_ENABLED 0
        #else
            #define KMX_AIO_CONTRACTS_ENABLED 1
        #endif
    #endif

    #if KMX_AIO_CONTRACTS_ENABLED
    /// @brief Asserts a precondition of the enclosing function.
        #if !defined(KMX_AIO_EXPECTS)
            #define KMX_AIO_EXPECTS(condition) ::kmx::aio::knx::check_contract((condition), #condition)
        #endif
    /// @brief Asserts a postcondition of the enclosing function.
        #if !defined(KMX_AIO_ENSURES)
            #define KMX_AIO_ENSURES(condition) ::kmx::aio::knx::check_contract((condition), #condition)
        #endif
    #else
    /// @brief Asserts a precondition of the enclosing function; compiled out in this build.
        #if !defined(KMX_AIO_EXPECTS)
            #define KMX_AIO_EXPECTS(condition) static_cast<void>(0)
        #endif
    /// @brief Asserts a postcondition of the enclosing function; compiled out in this build.
        #if !defined(KMX_AIO_ENSURES)
            #define KMX_AIO_ENSURES(condition) static_cast<void>(0)
        #endif
    #endif
#endif // KMX_AIO_FEATURE_KNX
