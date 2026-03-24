/// @file aio/completion/executor.hpp
/// @brief Completion-model executor using io_uring for asynchronous I/O.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <atomic>
    #include <cstdint>
    #include <expected>
    #include <liburing.h>
    #include <memory>
    #include <span>
    #include <stop_token>
    #include <system_error>

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/executor_base.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::completion
{
    /// @brief Configuration for the completion-model executor.
    struct executor_config
    {
        std::uint32_t ring_entries = 256u;    ///< Number of SQ entries in the io_uring instance.
        std::uint32_t max_completions = 256u; ///< Maximum CQE batch size per reap cycle.
        std::uint32_t thread_count = 1u;      ///< Number of worker threads for coroutine resumption.
        int core_id = -1;                     ///< CPU core affinity (-1 = no pinning).
    };

    /// @brief Statistics for io_uring operations and executor performance.
    struct statistics
    {
        std::atomic_uint64_t total_submissions {};     ///< Total SQEs submitted.
        std::atomic_uint64_t total_completions {};     ///< Total CQEs reaped.
        std::atomic_uint64_t total_tasks_spawned {};   ///< Total top-level tasks spawned.
        std::atomic_uint64_t total_tasks_completed {}; ///< Total top-level tasks completed.
        std::atomic_uint64_t error_count {};           ///< Total errors encountered.
        std::atomic_uint64_t submission_full_count {}; ///< Times the SQ was full.

        /// @brief Resets all counters to zero.
        void reset() noexcept;
    };

    /// @brief Completion-model executor using Linux io_uring.
    /// @details Implements a share-nothing, thread-per-core reactor. The kernel
    ///          performs the actual I/O and delivers completed buffers via the
    ///          completion queue. Coroutines are resumed when their specific
    ///          operation completes with a result code and byte count.
    class executor: public executor_base, public std::enable_shared_from_this<executor>
    {
    public:
        /// @brief Constructs the executor and initializes the io_uring instance.
        /// @param config Executor configuration.
        /// @throws std::system_error if io_uring_queue_init fails.
        /// @throws std::bad_alloc if internal allocations fail.
        explicit executor(const executor_config& config = {}) noexcept(false);

        ~executor() noexcept;

        /// @brief Non-copyable.
        executor(const executor&) = delete;
        /// @brief Non-copyable.
        executor& operator=(const executor&) = delete;

        /// @brief Prepares an asynchronous read into the provided buffer.
        /// @param fd     File descriptor to read from.
        /// @param buffer Destination buffer (kernel writes directly here).
        /// @param offset File offset (0 for sockets).
        /// @return A task yielding the number of bytes read, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] task<std::expected<std::size_t, std::error_code>> async_read(const fd_t fd, std::span<char> buffer,
                                                                                   const std::uint64_t offset = 0u) noexcept(false);

        /// @brief Prepares an asynchronous write from the provided buffer.
        /// @param fd     File descriptor to write to.
        /// @param buffer Source buffer.
        /// @param offset File offset (0 for sockets).
        /// @return A task yielding the number of bytes written, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] task<std::expected<std::size_t, std::error_code>> async_write(const fd_t fd, std::span<const char> buffer,
                                                                                    const std::uint64_t offset = 0u) noexcept(false);

        /// @brief Registers a set of memory buffers with the kernel for zero-copy I/O.
        /// @param iovecs Array of iovec structures describing the buffers.
        /// @return Success or error.
        [[nodiscard]] std::expected<void, std::error_code> register_buffers(std::span<const ::iovec> iovecs) noexcept;

        /// @brief Unregisters previously registered memory buffers.
        /// @return Success or error.
        [[nodiscard]] std::expected<void, std::error_code> unregister_buffers() noexcept;

        /// @brief Prepares an asynchronous read into a pre-registered buffer.
        /// @param fd        File descriptor to read from.
        /// @param buffer    Destination buffer (must be within a registered region).
        /// @param offset    File offset (0 for sockets).
        /// @param buf_index The index of the registered buffer.
        /// @return A task yielding the number of bytes read, or an error.
        [[nodiscard]] task<std::expected<std::size_t, std::error_code>> async_read_fixed(const fd_t fd, std::span<char> buffer,
                                                                                         const std::uint64_t offset,
                                                                                         const int buf_index) noexcept(false);

        /// @brief Prepares an asynchronous write from a pre-registered buffer.
        /// @param fd        File descriptor to write to.
        /// @param buffer    Source buffer (must be within a registered region).
        /// @param offset    File offset (0 for sockets).
        /// @param buf_index The index of the registered buffer.
        /// @return A task yielding the number of bytes written, or an error.
        [[nodiscard]] task<std::expected<std::size_t, std::error_code>> async_write_fixed(const fd_t fd, std::span<const char> buffer,
                                                                                          const std::uint64_t offset,
                                                                                          const int buf_index) noexcept(false);

        /// @brief Prepares an asynchronous accept on a listening socket.
        /// @param listen_fd The listening socket file descriptor.
        /// @param addr      Storage for the peer address.
        /// @param addrlen   Pointer to the address length.
        /// @return A task yielding the accepted client fd, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] task<std::expected<fd_t, std::error_code>> async_accept(const fd_t listen_fd, sockaddr_storage& addr,
                                                                              socklen_t& addrlen) noexcept(false);

        /// @brief Prepares an asynchronous connect.
        /// @param fd   Socket file descriptor.
        /// @param addr Destination address.
        /// @param addrlen Address length.
        /// @return A task yielding success or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] task<std::expected<void, std::error_code>> async_connect(const fd_t fd, const sockaddr* addr,
                                                                               const socklen_t addrlen) noexcept(false);

        /// @brief Prepares an asynchronous recvmsg.
        /// @param fd   Socket file descriptor.
        /// @param msg  Message header describing buffers and peer address.
        /// @param flags Flags forwarded to recvmsg.
        /// @return A task yielding the number of bytes received, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] task<std::expected<std::size_t, std::error_code>> async_recvmsg(const fd_t fd, msghdr* msg,
                                                                                      const unsigned int flags = 0u) noexcept(false);

        /// @brief Prepares an asynchronous sendmsg.
        /// @param fd   Socket file descriptor.
        /// @param msg  Message header describing buffers and peer address.
        /// @param flags Flags forwarded to sendmsg.
        /// @return A task yielding the number of bytes sent, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] task<std::expected<std::size_t, std::error_code>> async_sendmsg(const fd_t fd, const msghdr* msg,
                                                                                      const unsigned int flags = 0u) noexcept(false);

        /// @brief Prepares an asynchronous cancellation of an in-flight operation.
        /// @param user_data The user_data of the SQE to cancel.
        /// @return A task yielding success or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] task<std::expected<void, std::error_code>> async_cancel(const std::uint64_t user_data) noexcept(false);

        /// @brief Submits a root task to the system.
        /// @param t The top-level coroutine task.
        /// @throws std::bad_alloc if scheduling fails.
        void spawn(task<void>&& t) noexcept(false);

        /// @brief Runs the completion event loop. Blocks until stop() is called.
        /// @throws std::system_error on io_uring errors.
        void run() noexcept(false);

        /// @brief Signals the executor to stop processing.
        void stop() noexcept;

        /// @brief Returns a reference to the executor's statistics.
        [[nodiscard]] const statistics& get_stats() const noexcept { return metrics_; }

        /// @brief Resets all statistics counters.
        void reset_stats() noexcept { metrics_.reset(); }

        /// @brief Per-operation context passed through io_uring user_data.
        struct io_context
        {
            std::coroutine_handle<> continuation {}; ///< Coroutine to resume on completion.
            int result {};                           ///< Result from the CQE (bytes or -errno).
        };

        /// @brief Awaiter that suspends a coroutine until an io_uring CQE arrives.
        /// @details Stores the coroutine handle inside the io_context so that
        ///          process_completions() can resume it when the kernel signals completion.
        struct io_awaiter
        {
            io_context& ctx;

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> h) noexcept { ctx.continuation = h; }

            void await_resume() const noexcept {}
        };

    private:
        /// @brief Submits all pending SQEs to the kernel.
        /// @return Number of submitted entries, or error.
        [[nodiscard]] std::expected<std::size_t, std::error_code> submit() noexcept;

        /// @brief Reaps completions and resumes suspended coroutines.
        void process_completions() noexcept;

        /// @brief The main event loop running on the I/O thread.
        void event_loop(std::stop_token st) noexcept;

        /// @brief Pins the calling thread to the configured CPU core.
        void pin_to_core() const noexcept;

        /// @brief Detached wrapper for spawned top-level tasks.
        struct detached_task_wrapper
        {
            struct promise_type
            {
                detached_task_wrapper get_return_object() noexcept
                {
                    return detached_task_wrapper {std::coroutine_handle<promise_type>::from_promise(*this)};
                }

                std::suspend_always initial_suspend() const noexcept { return {}; }

                struct final_awaiter
                {
                    bool await_ready() const noexcept { return false; }
                    void await_suspend(std::coroutine_handle<promise_type> h) const noexcept { h.destroy(); }
                    void await_resume() const noexcept {}
                };

                final_awaiter final_suspend() const noexcept { return {}; }
                void unhandled_exception() noexcept { std::terminate(); }
                void return_void() const noexcept {}
            };

            std::coroutine_handle<promise_type> handle;
        };

        detached_task_wrapper execute_task(task<void> t, std::shared_ptr<executor> self) noexcept;

        executor_config config_;
        ::io_uring ring_ {};

        mutable statistics metrics_;
    };

} // namespace kmx::aio::completion
