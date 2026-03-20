/// @file aio/readiness/openonload/extensions.hpp
/// @brief OpenOnload runtime integration for kmx-aio.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#ifndef PCH
    #include <string_view>
    #include <expected>
    #include <system_error>
    #include <cstdint>
    #include <span>
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
    /// @brief Configures process-wide OpenOnload stack.
    inline bool initialize_runtime_stack(const char* stack_name = "kmxaio_fast_stack") noexcept
    {
#if KMX_AIO_OPENONLOAD_EXTENSIONS_AVAILABLE
        // Reserve a unique accelerated stack name and instruct all threads
        // to map to it when manipulating sockets bypassing the kernel stack.
        int rc = ::onload_set_stackname(ONLOAD_ALL_THREADS, ONLOAD_SCOPE_PROCESS, stack_name);
        return rc == 0;
#else
        (void)stack_name;
        // Without extensions, fallback to relying on LD_PRELOAD behavior safely.
        return false;
#endif
    }

    /// @brief Determines if an active file descriptor is bypass-accelerated.
    inline bool is_accelerated_fd(int fd) noexcept
    {
#if KMX_AIO_OPENONLOAD_EXTENSIONS_AVAILABLE
        const int stat = ::onload_fd_stat(fd);
        // ONLOAD_FD_STAT_OOF or positive structural index means accelerated hardware path.
        return stat == ONLOAD_FD_STAT_OOF || stat > 0;
#else
        (void)fd;
        return false; // Cannot reliably compute without onload_ext link
#endif
    }

    /// @brief Tries to read from zero-copy accelerated payload and store into managed buffer safely.
    inline std::expected<std::size_t, std::error_code> zero_copy_receive(int fd, std::span<char> buffer) noexcept
    {
#if KMX_AIO_OPENONLOAD_EXTENSIONS_AVAILABLE
        onload_zc_recv_args args {};

        // Issue zero-copy receive request grabbing buffers out of NIC ring memory natively
        int rc = ::onload_zc_recv(fd, &args);

        if (rc < 0)
        {
            return std::unexpected(std::error_code(-rc, std::system_category()));
        }

        if (rc == 0)
        {
            return 0u; // EOF
        }

        std::size_t total_copied = 0;

        // For maximum safety bridging to std::span, copy from NIC queues to span,
        // and instantly release hardware memory buffers automatically.
        for (int i = 0; i < args.msg.iovlen; ++i)
        {
            const std::size_t chunk_len = static_cast<std::size_t>(args.msg.iov[i].iov_len);
            const std::size_t available_space = buffer.size() - total_copied;
            const std::size_t to_copy = std::min(chunk_len, available_space);

            std::memcpy(buffer.data() + total_copied, args.msg.iov[i].iov_base, to_copy);
            total_copied += to_copy;

            if (total_copied == buffer.size())
                break;
        }

        // Implicit release of Onload network buffers upon returning since onload_zc_keep() is not invoked.
        return total_copied;
#else
        (void)fd;
        (void)buffer;
        return std::unexpected(std::make_error_code(std::errc::function_not_supported));
#endif
    }

    /// @brief Tries to send a payload via zero-copy fast path directly to the NIC hardware queues.
    inline std::expected<std::size_t, std::error_code> zero_copy_send(int fd, std::span<const char> buffer) noexcept
    {
#if KMX_AIO_OPENONLOAD_EXTENSIONS_AVAILABLE
        if (buffer.empty())
            return 0u;

        onload_zc_mmsg zc_msg[1] {};
        
        // Ask Onload to allocate transmit hardware buffers.
        // Sync flag behaves predictably for typical event loops, ensuring buffering.
        int rc = ::onload_zc_alloc_buffers(fd, &zc_msg[0].iov, 1, ONLOAD_ZC_SEND_SYNC);
        if (rc <= 0)
        {
            // If hardware buffers are unavailable, returning an error allows the caller
            // to fall back to the standard POSIX send(), which uses kernel/software queues.
            return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
        }

        const std::size_t available_capacity = static_cast<std::size_t>(zc_msg[0].iov[0].iov_len);
        const std::size_t to_copy = std::min(buffer.size(), available_capacity);
        
        auto* payload = reinterpret_cast<char*>(zc_msg[0].iov[0].iov_base);
        std::memcpy(payload, buffer.data(), to_copy);
        zc_msg[0].iov[0].iov_len = to_copy;
        zc_msg[0].fd = fd;

        // Push directly to the hardware transmit ring.
        rc = ::onload_zc_send(zc_msg, 1, 0);

        if (rc < 0)
        {
            return std::unexpected(std::error_code(-rc, std::system_category()));
        }

        return to_copy;
#else
        (void)fd;
        (void)buffer;
        return std::unexpected(std::make_error_code(std::errc::function_not_supported));
#endif
    }

} // namespace kmx::aio::readiness::openonload
