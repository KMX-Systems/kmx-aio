/// @file aio/detail/syscalls.hpp
/// @brief A two-part seam over the system calls whose failures the library reacts to.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// A handful of branches in this library exist only to handle a system call failing: epoll_wait
/// returning EINTR, io_uring_submit refusing a submission, a core pin being rejected. They are the
/// branches that matter most when they finally run, and the ones that never run in a test - a machine
/// does not fail its syscalls on request.
///
/// This seam makes them reachable, and it is split in two on purpose:
///
///   * `native_syscalls` holds the real calls. It is *declared* here and *defined* in
///     src/kmx/aio/detail/syscalls.cpp, so the headers the calls need - <sys/epoll.h>, <fcntl.h>,
///     <sys/socket.h> - stay inside that one translation unit instead of reaching every file that
///     wants to make a call fail. Only the types that appear in a signature are named here, and
///     `epoll_event` is named by forward declaration alone.
///
///   * `basic_syscalls<injects_faults>` stands in front of it. It has no primary definition: the two
///     specializations below are the whole of it, and they share nothing but their signatures.
///     `basic_syscalls<false>` is a straight forward to native_syscalls with no fault-handling code in
///     it at all - not a discarded branch, not a folded one, none written. `basic_syscalls<true>`
///     consults the registry before each call, and is compiled only when KMX_AIO_FAULT_INJECTION is
///     defined, which kmx_instrumentation sets alongside the coverage flags.
///
/// Writing the two apart rather than as one body under `if constexpr` is what lets the production
/// specialization say plainly what it is. The `syscalls` alias below picks between them, and
/// static_assert(!syscalls::injects_faults) states which one a production build got.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>

    // pthread_t and cpu_set_t are typedefs of opaque or anonymous types, so unlike epoll_event they
    // cannot be forward declared. These two are the only system headers the seam cannot shed.
    #include <pthread.h>
    #include <sched.h>
#endif

#if defined(KMX_AIO_FAULT_INJECTION)
    #ifndef PCH
        #include <array>
        #include <atomic>
        #include <cerrno>
    #endif
#endif

/// @brief Declared rather than included: only its address crosses the seam.
struct epoll_event;

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

    /// @brief The system calls this library needs to be able to fail. Defined in syscalls.cpp.
    /// @note Nothing calls this directly: it is the far side of the seam, and the library goes through
    ///       `syscalls` so that a test can get in between.
    struct native_syscalls
    {
        /// @brief Forwards to ::epoll_create1.
        [[nodiscard]] static int epoll_create1(int flags) noexcept;

        /// @brief Forwards to ::epoll_wait.
        [[nodiscard]] static int epoll_wait(int epfd, ::epoll_event* events, int max_events, int timeout_ms) noexcept;

        /// @brief Forwards to ::fcntl.
        [[nodiscard]] static int fcntl(int fd, int cmd, int arg) noexcept;

        /// @brief Forwards to ::socket.
        [[nodiscard]] static int socket(int domain, int type, int protocol) noexcept;

        /// @brief Forwards to ::pthread_setaffinity_np.
        [[nodiscard]] static int pthread_setaffinity_np(::pthread_t thread, std::size_t size, const ::cpu_set_t* set) noexcept;

        /// @brief Forwards to ::pthread_getaffinity_np.
        [[nodiscard]] static int pthread_getaffinity_np(::pthread_t thread, std::size_t size, ::cpu_set_t* set) noexcept;
    };

    /// @brief The seam in front of native_syscalls. Only the two specializations below exist.
    template <bool injects_faults>
    struct basic_syscalls;

    /// @brief The production seam: each call is nothing but a forward to native_syscalls.
    template <>
    struct basic_syscalls<false>
    {
        /// @brief False: this specialization carries no fault-injection code.
        static constexpr bool injects_faults = false;

        /// @brief Wrapper for ::epoll_create1.
        [[nodiscard]] static int epoll_create1(const int flags) noexcept { return native_syscalls::epoll_create1(flags); }

        /// @brief Wrapper for ::epoll_wait.
        [[nodiscard]] static int epoll_wait(const int epfd, ::epoll_event* const events, const int max_events,
                                            const int timeout_ms) noexcept
        {
            return native_syscalls::epoll_wait(epfd, events, max_events, timeout_ms);
        }

        /// @brief Wrapper for ::fcntl.
        [[nodiscard]] static int fcntl(const int fd, const int cmd, const int arg) noexcept { return native_syscalls::fcntl(fd, cmd, arg); }

        /// @brief Wrapper for ::socket.
        [[nodiscard]] static int socket(const int domain, const int type, const int protocol) noexcept
        {
            return native_syscalls::socket(domain, type, protocol);
        }

        /// @brief Wrapper for ::pthread_setaffinity_np.
        [[nodiscard]] static int pthread_setaffinity_np(const ::pthread_t thread, const std::size_t size,
                                                        const ::cpu_set_t* const set) noexcept
        {
            return native_syscalls::pthread_setaffinity_np(thread, size, set);
        }

        /// @brief Wrapper for ::pthread_getaffinity_np.
        [[nodiscard]] static int pthread_getaffinity_np(const ::pthread_t thread, const std::size_t size, ::cpu_set_t* const set) noexcept
        {
            return native_syscalls::pthread_getaffinity_np(thread, size, set);
        }
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

    /// @brief Arms a fault for the duration of a scope and disarms it however the scope ends.
    class scoped_fault
    {
    public:
        /// @param id    The call to fail.
        /// @param error The errno the call should report.
        /// @param times How many calls to fail before letting them through again.
        /// @param skip  How many calls to let through before the first failure.
        scoped_fault(const syscall_id id, const int error, const unsigned times = 1u, const unsigned skip = 0u) noexcept: id_(id)
        {
            fault_registry::arm(id, error, times, skip);
        }

        scoped_fault(const scoped_fault&) = delete;
        scoped_fault& operator=(const scoped_fault&) = delete;

        ~scoped_fault() noexcept { fault_registry::disarm(id_); }

    private:
        syscall_id id_;
    };

    /// @brief The testing seam: each call asks the registry for a failure before forwarding.
    /// @note Compiled only under KMX_AIO_FAULT_INJECTION, so a production build has no definition of
    ///       this specialization to instantiate even by mistake.
    template <>
    struct basic_syscalls<true>
    {
        /// @brief True: this specialization carries the fault-injection stubs.
        static constexpr bool injects_faults = true;

        /// @brief Stub for ::epoll_create1.
        [[nodiscard]] static int epoll_create1(const int flags) noexcept
        {
            if (const int error = fault_registry::take(syscall_id::epoll_create1); error != 0)
            {
                errno = error;
                return -1;
            }

            return native_syscalls::epoll_create1(flags);
        }

        /// @brief Stub for ::epoll_wait.
        [[nodiscard]] static int epoll_wait(const int epfd, ::epoll_event* const events, const int max_events,
                                            const int timeout_ms) noexcept
        {
            if (const int error = fault_registry::take(syscall_id::epoll_wait); error != 0)
            {
                errno = error;
                return -1;
            }

            return native_syscalls::epoll_wait(epfd, events, max_events, timeout_ms);
        }

        /// @brief Stub for ::fcntl.
        [[nodiscard]] static int fcntl(const int fd, const int cmd, const int arg) noexcept
        {
            if (const int error = fault_registry::take(syscall_id::fcntl); error != 0)
            {
                errno = error;
                return -1;
            }

            return native_syscalls::fcntl(fd, cmd, arg);
        }

        /// @brief Stub for ::socket.
        [[nodiscard]] static int socket(const int domain, const int type, const int protocol) noexcept
        {
            if (const int error = fault_registry::take(syscall_id::socket); error != 0)
            {
                errno = error;
                return -1;
            }

            return native_syscalls::socket(domain, type, protocol);
        }

        /// @brief Stub for ::pthread_setaffinity_np.
        /// @note The pthread calls report an errno as their return value and leave the global alone, so
        ///       an injected failure is returned rather than stored.
        [[nodiscard]] static int pthread_setaffinity_np(const ::pthread_t thread, const std::size_t size,
                                                        const ::cpu_set_t* const set) noexcept
        {
            if (const int error = fault_registry::take(syscall_id::pthread_setaffinity_np); error != 0)
                return error;

            return native_syscalls::pthread_setaffinity_np(thread, size, set);
        }

        /// @brief Stub for ::pthread_getaffinity_np.
        [[nodiscard]] static int pthread_getaffinity_np(const ::pthread_t thread, const std::size_t size, ::cpu_set_t* const set) noexcept
        {
            if (const int error = fault_registry::take(syscall_id::pthread_getaffinity_np); error != 0)
                return error;

            return native_syscalls::pthread_getaffinity_np(thread, size, set);
        }
    };
#endif

#if defined(KMX_AIO_FAULT_INJECTION)
    /// @brief The seam the library calls through, in a fault-injection build.
    using syscalls = basic_syscalls<true>;
#else
    /// @brief The seam the library calls through. Nothing but a call to the kernel wrapper is left.
    using syscalls = basic_syscalls<false>;
    static_assert(!syscalls::injects_faults, "the production seam must carry no fault-injection code");
#endif

} // namespace kmx::aio::detail
