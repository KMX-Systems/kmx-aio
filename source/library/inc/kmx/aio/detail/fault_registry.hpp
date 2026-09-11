/// @file inc/kmx/aio/detail/fault_registry.hpp
/// @brief The calls the system-call seam can fail, and the process-wide registry of failures armed against them.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// `syscall_id` is declared in every build: the io_uring and OpenSSL seams name it whether or not they
/// inject faults. `fault_registry` itself is compiled only when KMX_AIO_FAULT_INJECTION is defined.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif

#if defined(KMX_AIO_FAULT_INJECTION)
    #ifndef PCH
        #include <array>
        #include <atomic>
        #include <cstddef>
    #endif
#endif

namespace kmx::aio::detail
{
    /// @brief Names the calls that can be made to fail.
    /// @note Only calls whose failure the library actually branches on appear here. Wrapping a call
    ///       that has no failure path would add a seam with nothing behind it.
    enum class syscall_id : std::uint8_t
    {
        /// @brief `epoll_create1` — creating the readiness executor's epoll instance.
        epoll_create1,
        /// @brief `epoll_wait` — reaping readiness events.
        epoll_wait,
        /// @brief `fcntl` — querying or changing descriptor flags.
        fcntl,
        /// @brief `io_uring_queue_init` — creating the completion executor's ring.
        io_uring_queue_init,
        /// @brief `io_uring_submit` — handing prepared SQEs to the kernel.
        io_uring_submit,
        /// @brief `io_uring_wait_cqe_timeout` — waiting for a completion with a deadline.
        io_uring_wait_cqe_timeout,
        /// @brief `io_uring_submit_and_wait_timeout` — the event loop's combined submit-and-wait.
        io_uring_submit_and_wait_timeout,
        /// @brief `pthread_setaffinity_np` — pinning a thread to its configured core.
        pthread_setaffinity_np,
        /// @brief `pthread_getaffinity_np` — reading back a thread's core affinity.
        pthread_getaffinity_np,
        /// @brief `socket` — creating a socket descriptor.
        socket,
        /// @brief `BIO_new` — creating an OpenSSL BIO for the TLS streams.
        bio_new,
        /// @brief Number of wrappable calls; not a call itself.
        count
    };

#if defined(KMX_AIO_FAULT_INJECTION)
    /// @brief Armed failures, shared across every thread in the process.
    /// @details Deliberately not thread_local. The call a test wants to fail runs on the executor's I/O
    ///          thread, not on the thread that armed it, so a per-thread registry would never fire.
    class fault_registry
    {
    public:
        /// @brief Arms calls of @p id to fail with @p error.
        /// @param skip  How many calls to let through untouched first. Needed wherever the call to fail
        ///              is not the first of its kind on the path - set_as_non_blocking() issues F_GETFL
        ///              before the F_SETFL whose failure branch is under test.
        /// @param times How many calls to fail once @p skip have passed.
        static void arm(const syscall_id id, const int error, const unsigned times, const unsigned skip = 0u) noexcept
        {
            auto& slot = slot_for(id);
            slot.error.store(error, std::memory_order_release);
            slot.skip.store(skip, std::memory_order_release);
            slot.remaining.store(times, std::memory_order_release);
        }

        /// @brief Disarms @p id.
        static void disarm(const syscall_id id) noexcept
        {
            auto& slot = slot_for(id);
            slot.remaining.store(0u, std::memory_order_release);
            slot.skip.store(0u, std::memory_order_release);
        }

        /// @brief Disarms everything.
        static void clear() noexcept
        {
            for (auto& slot: slots())
            {
                slot.remaining.store(0u, std::memory_order_release);
                slot.skip.store(0u, std::memory_order_release);
            }
        }

        /// @brief Consumes one armed failure for @p id.
        /// @return The errno to report, or 0 when nothing is armed.
        [[nodiscard]] static int take(const syscall_id id) noexcept
        {
            auto& slot = slot_for(id);

            unsigned skip = slot.skip.load(std::memory_order_acquire);
            while (skip > 0u)
            {
                // LCOV_EXCL_BR_LINE: the retry arm needs two threads consuming the same armed slot at
                // once. A test arms a fault from one thread, so the exchange succeeds first time.
                if (slot.skip.compare_exchange_weak(skip, skip - 1u, std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) // LCOV_EXCL_BR_LINE
                    return 0;
            }

            unsigned remaining = slot.remaining.load(std::memory_order_acquire);
            while (remaining > 0u)
            {
                // LCOV_EXCL_BR_LINE: as above - the retry needs contention on one slot.
                if (slot.remaining.compare_exchange_weak(remaining, remaining - 1u, std::memory_order_acq_rel, // LCOV_EXCL_BR_LINE
                                                         std::memory_order_acquire))
                    return slot.error.load(std::memory_order_acquire);
            }

            return 0;
        }

    private:
        struct slot
        {
            std::atomic_int error {};
            std::atomic_uint remaining {};
            std::atomic_uint skip {};
        };

        using slot_array = std::array<slot, static_cast<std::size_t>(syscall_id::count)>;

        [[nodiscard]] static slot_array& slots() noexcept
        {
            static slot_array instance;
            return instance;
        }

        [[nodiscard]] static slot& slot_for(const syscall_id id) noexcept { return slots()[static_cast<std::size_t>(id)]; }
    };
#endif
}
