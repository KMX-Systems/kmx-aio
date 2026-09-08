/// @file kmx/aio/readiness/openonload/extensions.cpp
/// @brief The compiled body of the OpenOnload acceleration entry points.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/readiness/openonload/extensions.hpp>

namespace kmx::aio::readiness::openonload
{
    bool initialize_runtime_stack(const char* stack_name) noexcept
    {
#if KMX_AIO_OPENONLOAD_EXTENSIONS_AVAILABLE
        // Reserve a unique accelerated stack name and instruct all threads
        // to map to it when manipulating sockets bypassing the kernel stack.
        int rc = ::onload_set_stackname(ONLOAD_ALL_THREADS, ONLOAD_SCOPE_PROCESS, stack_name);
        return rc == 0;
#else
        (void) stack_name;
        // Without extensions, fallback to relying on LD_PRELOAD behavior safely.
        return false;
#endif
    }

    bool is_accelerated_fd(int fd) noexcept
    {
#if KMX_AIO_OPENONLOAD_EXTENSIONS_AVAILABLE
        const int stat = ::onload_fd_stat(fd);
        // ONLOAD_FD_STAT_OOF or positive structural index means accelerated hardware path.
        return (stat == ONLOAD_FD_STAT_OOF) || (stat > 0);
#else
        (void) fd;
        return false; // Cannot reliably compute without onload_ext link
#endif
    }

    expected_size_t zero_copy_receive(const int fd, span_char_t buffer) noexcept
    {
#if KMX_AIO_OPENONLOAD_EXTENSIONS_AVAILABLE
        onload_zc_recv_args args {};

        // Issue zero-copy receive request grabbing buffers out of NIC ring memory natively
        int rc = ::onload_zc_recv(fd, &args);

        if (rc < 0)
            return std::unexpected(std::error_code(-rc, std::system_category()));

        if (rc == 0)
            return 0u; // EOF

        std::size_t total_copied {};

        // For maximum safety bridging to std::span, copy from NIC queues to span,
        // and instantly release hardware memory buffers automatically.
        for (int i {}; i < args.msg.iovlen; ++i)
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
        (void) fd;
        (void) buffer;
        return std::unexpected(std::make_error_code(std::errc::function_not_supported));
#endif
    }

    expected_size_t zero_copy_send(const int fd, cspan_char_t buffer) noexcept
    {
#if KMX_AIO_OPENONLOAD_EXTENSIONS_AVAILABLE
        if (buffer.empty())
            return 0u;

        onload_zc_mmsg zc_msg[1] {};

        // Ask Onload to allocate transmit hardware buffers.
        // Sync flag behaves predictably for typical event loops, ensuring buffering.
        int rc = ::onload_zc_alloc_buffers(fd, &zc_msg[0].iov, 1, ONLOAD_ZC_SEND_SYNC);
        if (rc <= 0)
            // If hardware buffers are unavailable, returning an error allows the caller
            // to fall back to the standard POSIX send(), which uses kernel/software queues.
            return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));

        const std::size_t available_capacity = static_cast<std::size_t>(zc_msg[0].iov[0].iov_len);
        const std::size_t to_copy = std::min(buffer.size(), available_capacity);

        auto* payload = reinterpret_cast<char*>(zc_msg[0].iov[0].iov_base);
        std::memcpy(payload, buffer.data(), to_copy);
        zc_msg[0].iov[0].iov_len = to_copy;
        zc_msg[0].fd = fd;

        // Push directly to the hardware transmit ring.
        rc = ::onload_zc_send(zc_msg, 1, 0);

        if (rc < 0)
            return std::unexpected(std::error_code(-rc, std::system_category()));

        return to_copy;
#else
        (void) fd;
        (void) buffer;
        return std::unexpected(std::make_error_code(std::errc::function_not_supported));
#endif
    }
}
