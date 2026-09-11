/// @file api/kmx/aio/readiness/openonload/extensions.hpp
/// @brief OpenOnload runtime integration for kmx-aio.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_READINESS)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>

        #include <cstdint>
        #include <expected>
        #include <span>
        #include <string_view>
        #include <system_error>
    #endif

    // Guard around actual Onload implementation
    #if defined(KMX_AIO_FEATURE_OPENONLOAD) && __has_include(<onload/extensions.h>)
        #include <onload/extensions.h>
        #define KMX_AIO_OPENONLOAD_EXTENSIONS_AVAILABLE 1
    #else
        #define KMX_AIO_OPENONLOAD_EXTENSIONS_AVAILABLE 0
        // Definitions for compiling cleanly even without vendor headers, though missing real runtime capability.
        #ifndef ONLOAD_ALL_THREADS
            #define ONLOAD_ALL_THREADS 1
        #endif
        #ifndef ONLOAD_SCOPE_PROCESS
            #define ONLOAD_SCOPE_PROCESS 1
        #endif
        #ifndef ONLOAD_FD_STAT_OOF
            #define ONLOAD_FD_STAT_OOF 3
        #endif
    #endif

namespace kmx::aio::readiness::openonload
{
    /// @brief Names the process-wide OpenOnload stack that the process's accelerated sockets are placed in.
    /// @param stack_name The stack name to request for every thread of the process.
    /// @return Nothing when Onload applied the name; std::errc::function_not_supported when the library was built
    ///         without <onload/extensions.h>, or error_code::openonload_init_failed when Onload refused the name.
    [[nodiscard]] expected_void_t initialize_runtime_stack(const char* stack_name = "kmxaio_fast_stack") noexcept;

    /// @brief Determines if an active file descriptor is bypass-accelerated.
    [[nodiscard]] bool is_accelerated_fd(int fd) noexcept;

    /// @brief Tries to read from zero-copy accelerated payload and store into managed buffer safely.
    [[nodiscard]] expected_size_t zero_copy_receive(const int fd, span_char_t buffer) noexcept;

    /// @brief Tries to send a payload via zero-copy fast path directly to the NIC hardware queues.
    [[nodiscard]] expected_size_t zero_copy_send(const int fd, cspan_char_t buffer) noexcept;

}
#endif // KMX_AIO_FEATURE_READINESS
