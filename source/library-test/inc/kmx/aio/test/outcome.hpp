/// @file aio/test/outcome.hpp
/// @brief Result-capture shapes for tests that drive an asynchronous operation.
/// @details A coroutine cannot hand its result back to the TEST_CASE that spawned it by returning, so
///          the test gives it somewhere to write instead and inspects that afterwards. These are the
///          three shapes the suite needs; they are deliberately kept apart rather than merged, because
///          which one is correct depends on whether the writer and the reader are the same thread.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <atomic>
    #include <cstddef>
    #include <optional>
    #include <system_error>

    #include <kmx/aio/basic_types.hpp>
#endif

namespace kmx::aio::test
{
    /// @brief What one asynchronous operation reported, for a test that drives the loop itself.
    /// @details Plain members, no synchronisation: the coroutine writing these runs on the same thread
    ///          as the run() call that resumed it, and the test reads them only after run() returned.
    /// @tparam value_t What the operation yields on success.
    template <typename value_t>
    struct outcome
    {
        bool completed {}; ///< Whether the coroutine ran to completion at all.
        bool ok {};        ///< Whether the operation succeeded.
        value_t value {};  ///< The success value; meaningful only when @ref ok.
        std::error_code error {};
    };

    using size_outcome = outcome<std::size_t>; ///< For operations yielding a byte count.
    using fd_outcome = outcome<fd_t>;          ///< For operations yielding a descriptor.
    using void_outcome = outcome<int>;         ///< For operations yielding nothing.

    /// @brief What one asynchronous operation reported, across a thread boundary.
    /// @details Distinct from @ref outcome, and deliberately not merged with it: when the loop runs on
    ///          its own thread the flags are written there and read here, so plain members would be a
    ///          data race. Giving the single-threaded cases atomics instead would cost them nothing but
    ///          would hide which tests actually cross a thread.
    struct atomic_outcome
    {
        std::atomic_bool completed {false}; ///< Set once the awaiting task ran to completion.
        std::atomic_bool ok {false};        ///< What the operation reported.
        std::atomic_size_t value {0u};      ///< The success value; meaningful only when @ref ok.
        std::error_code error {};
    };

    /// @brief What a single wait_io() did, as seen from the test thread.
    struct wait_outcome
    {
        std::atomic_bool parked {false};    ///< Set immediately before the wait is awaited.
        std::atomic_bool completed {false}; ///< Set once the awaiting task ran to completion.
        std::atomic_bool fired {false};     ///< What the wait reported: an event (true) or a cancel.
    };

    /// @brief Holds whatever a service call returned, for tests that care about the whole result.
    /// @details Where @ref outcome flattens success into a bool and a value, this keeps the expected as
    ///          it came back, which is what the client-service tests assert against.
    /// @tparam Result The awaited expression's result type.
    template <typename Result>
    struct coroutine_result_state
    {
        std::optional<Result> result; ///< Engaged once the operation returned.
        bool completed {};            ///< Whether the coroutine ran to completion at all.
    };

} // namespace kmx::aio::test
