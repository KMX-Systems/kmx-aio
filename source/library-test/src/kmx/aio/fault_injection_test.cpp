/// @file aio/fault_injection_test.cpp
/// @brief Tests for the branches that only run when a system call fails.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// These are the branches that matter most when they finally run in production and that a machine will
/// never produce on request: epoll_wait interrupted by a signal, io_uring refusing a submission, a core
/// pin rejected by the scheduler. They reach them through the seam in aio/detail/syscalls.hpp, which
/// compiles its faulting policy in only under KMX_AIO_FAULT_INJECTION.
///
/// The whole file is compiled out otherwise, so a build without the seam still builds and runs the
/// suite - it just does not carry these cases.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/detail/syscalls.hpp>

#if defined(KMX_AIO_FAULT_INJECTION)

    #include <array>
    #include <atomic>
    #include <cerrno>
    #include <chrono>
    #include <memory>
    #include <span>
    #include <system_error>
    #include <thread>
    #include <vector>

    #include <fcntl.h>
    #include <sys/socket.h>
    #include <unistd.h>

    #include <kmx/aio/completion/detail/uring_syscalls.hpp>
    #include <openssl/ssl.h>

    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/completion/udp/endpoint.hpp>
    #include <kmx/aio/completion/udp/socket.hpp>
    #include <kmx/aio/file_descriptor.hpp>
    #include <kmx/aio/readiness/descriptor/epoll.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/task.hpp>
    #include <kmx/aio/test/executor_runner.hpp>
    #include <kmx/aio/tls/stream.hpp>

namespace kmx::aio
{
    using namespace std::literals::chrono_literals;
    using detail::scoped_fault;
    using detail::syscall_id;
    using kmx::aio::test::scoped_runner;
    using kmx::aio::test::wait_for_flag;

    namespace
    {
        /// @brief A connected pair of non-blocking sockets, closed on destruction.
        class socket_pair
        {
        public:
            socket_pair() noexcept { valid_ = ::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds_) == 0; }

            socket_pair(const socket_pair&) = delete;
            socket_pair& operator=(const socket_pair&) = delete;

            ~socket_pair() noexcept
            {
                if (valid_)
                {
                    ::close(fds_[0]);
                    ::close(fds_[1]);
                }
            }

            [[nodiscard]] bool valid() const noexcept { return valid_; }
            [[nodiscard]] int local() const noexcept { return fds_[0]; }
            [[nodiscard]] int peer() const noexcept { return fds_[1]; }

        private:
            int fds_[2] {-1, -1};
            bool valid_ = false;
        };

        /// @brief A pipe whose ends are closed on destruction.
        class pipe_pair
        {
        public:
            pipe_pair() noexcept { valid_ = ::pipe(fds_) == 0; }

            pipe_pair(const pipe_pair&) = delete;
            pipe_pair& operator=(const pipe_pair&) = delete;

            ~pipe_pair() noexcept
            {
                if (valid_)
                {
                    ::close(fds_[0]);
                    ::close(fds_[1]);
                }
            }

            [[nodiscard]] bool valid() const noexcept { return valid_; }
            [[nodiscard]] int read_end() const noexcept { return fds_[0]; }
            [[nodiscard]] int write_end() const noexcept { return fds_[1]; }

        private:
            int fds_[2] {-1, -1};
            bool valid_ = false;
        };

        /// @brief The least an inner stream has to be for tls::stream to hold one.
        struct tls_stub_stream
        {
            int id {};
        };

        /// @brief Waits until the executor's I/O thread exists.
        /// @details A spawned task can start on a scheduler worker before run() has created the I/O
        ///          thread, so a task reporting "parked" is not evidence that the thread is up yet.
        ///          is_io_thread_affined_to reports operation_not_permitted until it is, which is the
        ///          signal to wait on before arming a fault for the affinity query itself.
        template <typename executor_t>
        [[nodiscard]] bool wait_for_io_thread(executor_t& exec, const std::chrono::milliseconds limit)
        {
            const auto deadline = std::chrono::steady_clock::now() + limit;
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (exec.is_io_thread_affined_to(0).has_value())
                    return true;

                std::this_thread::sleep_for(1ms);
            }

            return false;
        }
    }

    // =========================================================================
    // the seam itself
    // =========================================================================

    TEST_CASE("the production seam carries no fault-checking code", "[fault][seam]")
    {
        // The property that makes the seam acceptable in this library: the production specialization is
        // written apart from the faulting one and has no fault-handling code in it at all - not a
        // discarded branch, not a folded one. What is left is the forward into the native wrapper.
        static_assert(!detail::basic_syscalls<false>::injects_faults);
        static_assert(!completion::detail::basic_uring_syscalls<false>::injects_faults);
        static_assert(!tls::detail::basic_tls_syscalls<false>::injects_faults);
        static_assert(detail::basic_syscalls<true>::injects_faults);
        SUCCEED("the <false> specializations carry nothing but the forward");
    }

    TEST_CASE("an armed fault fires once and then stops", "[fault][seam]")
    {
        int flags {};
        {
            const scoped_fault fault {syscall_id::fcntl, EPERM, 1u};

            auto fd = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
            REQUIRE(fd.has_value());

            const auto first = fd->fcntl(F_GETFL, 0);
            REQUIRE_FALSE(first.has_value());
            CHECK(first.error() == std::errc::operation_not_permitted);

            // The arm was for a single call, so the next one reaches the kernel.
            const auto second = fd->fcntl(F_GETFL, 0);
            REQUIRE(second.has_value());
            flags = *second;
        }

        CHECK(flags >= 0);
    }

    TEST_CASE("a fault is disarmed when its scope ends", "[fault][seam]")
    {
        {
            const scoped_fault fault {syscall_id::epoll_create1, EMFILE, 4u};
            CHECK_FALSE(readiness::descriptor::epoll::create().has_value());
        }

        // Four failures were armed and only one consumed; leaving the scope has to drop the rest, or
        // every later test in the binary inherits them.
        CHECK(readiness::descriptor::epoll::create().has_value());
    }

    // =========================================================================
    // epoll
    // =========================================================================

    TEST_CASE("epoll::create reports a descriptor-table exhaustion", "[fault][readiness][epoll]")
    {
        const scoped_fault fault {syscall_id::epoll_create1, EMFILE, 1u};

        const auto created = readiness::descriptor::epoll::create();
        REQUIRE_FALSE(created.has_value());
        CHECK(created.error() == std::errc::too_many_files_open);
    }

    TEST_CASE("the readiness executor refuses to construct without an epoll instance", "[fault][readiness][executor]")
    {
        // epoll_create1 failing is the one error the constructor cannot carry on from, so it throws
        // rather than hand back an executor with no backing descriptor.
        const scoped_fault fault {syscall_id::epoll_create1, ENOMEM, 1u};
        CHECK_THROWS_AS(readiness::executor {}, std::system_error);
    }

    TEST_CASE("epoll wait_events reports a failed wait", "[fault][readiness][epoll]")
    {
        auto ep = readiness::descriptor::epoll::create();
        REQUIRE(ep.has_value());

        std::vector<::epoll_event> events;
        const scoped_fault fault {syscall_id::epoll_wait, EBADF, 1u};

        const auto result = ep->wait_events(events, 8, 0);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::bad_file_descriptor);
    }

    // =========================================================================
    // the readiness event loop
    // =========================================================================

    TEST_CASE("the readiness loop retries an interrupted epoll_wait", "[fault][readiness][executor]")
    {
        // EINTR is not a failure: a signal landing on the I/O thread must not cost the loop an
        // iteration, let alone tear it down. The wait that follows has to deliver the event normally.
        socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_shared<readiness::executor>();
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        std::atomic_bool parked {false};
        std::atomic_bool fired {false};
        std::atomic_bool done {false};

        auto body = [&parked, &fired, &done, exec, fd = sockets.local()]() -> task<void>
        {
            parked.store(true, std::memory_order_release);
            const bool event = co_await exec->wait_io(fd, readiness::event_type::read);
            fired.store(event, std::memory_order_release);
            done.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(parked, 2s));

        {
            const scoped_fault fault {syscall_id::epoll_wait, EINTR, 3u};
            std::this_thread::sleep_for(50ms);
        }

        const char byte = 'x';
        REQUIRE(::write(sockets.peer(), &byte, 1u) == 1);

        REQUIRE(wait_for_flag(done, 5s));
        CHECK(fired.load(std::memory_order_acquire));
        CHECK(exec->get_stats().error_count.load() == 0u);
    }

    TEST_CASE("the readiness loop stops on a failing epoll_wait", "[fault][readiness][executor]")
    {
        // Anything other than EINTR means the loop can no longer make progress, so it counts the error
        // and leaves rather than spinning on a wait that will keep failing.
        socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_shared<readiness::executor>();
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        std::atomic_bool parked {false};
        auto body = [&parked, exec, fd = sockets.local()]() -> task<void>
        {
            parked.store(true, std::memory_order_release);
            const bool event = co_await exec->wait_io(fd, readiness::event_type::read);
            (void) event;
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(parked, 2s));

        const scoped_fault fault {syscall_id::epoll_wait, EBADF, 64u};

        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while ((exec->get_stats().error_count.load() == 0u) && (std::chrono::steady_clock::now() < deadline))
            std::this_thread::sleep_for(5ms);

        CHECK(exec->get_stats().error_count.load() > 0u);

        // Release the parked wait so the runner can shut down cleanly.
        exec->cancel_io(sockets.local());
    }

    // =========================================================================
    // file_descriptor
    // =========================================================================

    TEST_CASE("set_as_non_blocking reports a failing F_SETFL", "[fault][core][file_descriptor]")
    {
        // Two fcntl calls happen here, and only the second one's failure reaches the branch under test,
        // so the first is skipped rather than armed.
        auto fd = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(fd.has_value());

        const scoped_fault fault {syscall_id::fcntl, EPERM, 1u, /*skip=*/1u};

        const auto result = fd->set_as_non_blocking();
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::operation_not_permitted);
    }

    TEST_CASE("set_as_non_blocking reports a failing F_GETFL", "[fault][core][file_descriptor]")
    {
        auto fd = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(fd.has_value());

        const scoped_fault fault {syscall_id::fcntl, EACCES, 1u};

        const auto result = fd->set_as_non_blocking();
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::permission_denied);
    }

    // =========================================================================
    // io_uring
    // =========================================================================

    TEST_CASE("the completion executor refuses to construct without a ring", "[fault][completion][executor]")
    {
        const scoped_fault fault {syscall_id::io_uring_queue_init, ENOMEM, 1u};
        CHECK_THROWS_AS(completion::executor {}, std::system_error);
    }

    TEST_CASE("a refused submission is reported and counted", "[fault][completion][executor]")
    {
        // io_uring_submit failing has to reach the awaiting coroutine as an error rather than leave it
        // suspended on an operation the kernel never accepted.
        int fds[2] {-1, -1};
        REQUIRE(::pipe(fds) == 0);

        completion::executor exec;
        bool completed = false;
        bool ok = false;
        std::error_code error {};
        std::array<char, 8> buffer {};

        auto body = [&exec, &completed, &ok, &error, &buffer, fd = fds[0]]() -> task<void>
        {
            const scoped_fault fault {syscall_id::io_uring_submit, EAGAIN, 1u};
            const auto r = co_await exec.async_read(fd, std::span<char>(buffer.data(), buffer.size()));
            completed = true;
            if (r)
                ok = true;
            else
                error = r.error();

            exec.stop();
        };
        exec.spawn(body());
        exec.run();

        CHECK(completed);
        CHECK_FALSE(ok);
        CHECK(error == std::errc::resource_unavailable_try_again);
        CHECK(exec.get_stats().error_count.load() > 0u);

        ::close(fds[0]);
        ::close(fds[1]);
    }

    TEST_CASE("a failing completion wait is counted rather than fatal", "[fault][completion][executor]")
    {
        // ETIME and EINTR are ordinary loop outcomes; anything else is logged and counted, and the loop
        // keeps going so that the shutdown drain still gets to run.
        int fds[2] {-1, -1};
        REQUIRE(::pipe(fds) == 0);

        completion::executor exec;
        std::atomic_bool submitted {false};
        std::array<char, 8> buffer {};

        auto body = [&exec, &submitted, &buffer, fd = fds[0]]() -> task<void>
        {
            submitted.store(true, std::memory_order_release);
            const auto r = co_await exec.async_read(fd, std::span<char>(buffer.data(), buffer.size()));
            (void) r;
        };
        exec.spawn(body());

        kmx::aio::test::scoped_completion_runner runner {exec};
        REQUIRE(wait_for_flag(submitted, 2s));

        {
            const scoped_fault fault {syscall_id::io_uring_wait_cqe_timeout, EBADF, 2u};
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while ((exec.get_stats().error_count.load() == 0u) && (std::chrono::steady_clock::now() < deadline))
                std::this_thread::sleep_for(5ms);
        }

        CHECK(exec.get_stats().error_count.load() > 0u);

        exec.stop();
        REQUIRE(runner.wait_until_drained(std::chrono::seconds(5)));

        ::close(fds[0]);
        ::close(fds[1]);
    }

    // =========================================================================
    // core pinning
    // =========================================================================

    TEST_CASE("an interrupted completion wait is retried", "[fault][completion][executor]")
    {
        // ETIME and EINTR are the two answers the loop treats as ordinary: the first is the timeout it
        // asked for, the second a signal landing on the I/O thread. Neither may be counted as an error
        // or end the loop. The error case is covered above; this is the EINTR arm beside it.
        int fds[2] {-1, -1};
        REQUIRE(::pipe(fds) == 0);

        completion::executor exec;
        std::atomic_bool submitted {false};
        std::atomic_bool finished {false};
        std::array<char, 8> buffer {};

        auto body = [&exec, &submitted, &finished, &buffer, fd = fds[0]]() -> task<void>
        {
            submitted.store(true, std::memory_order_release);
            const auto r = co_await exec.async_read(fd, std::span<char>(buffer.data(), buffer.size()));
            (void) r;
            finished.store(true, std::memory_order_release);
        };
        exec.spawn(body());

        kmx::aio::test::scoped_completion_runner runner {exec};
        REQUIRE(wait_for_flag(submitted, 2s));

        const auto errors_before = exec.get_stats().error_count.load();

        {
            const scoped_fault fault {syscall_id::io_uring_wait_cqe_timeout, EINTR, 4u};
            std::this_thread::sleep_for(100ms);
        }

        // The read still completes normally once the pipe is written to, so the interruptions cost the
        // loop nothing but the iterations they replaced.
        const char byte = 'x';
        REQUIRE(::write(fds[1], &byte, 1u) == 1);
        REQUIRE(wait_for_flag(finished, 5s));
        CHECK(exec.get_stats().error_count.load() == errors_before);

        exec.stop();
        REQUIRE(runner.wait_until_drained(std::chrono::seconds(5)));

        ::close(fds[0]);
        ::close(fds[1]);
    }

    TEST_CASE("a rejected core pin does not stop the completion loop", "[fault][completion][affinity]")
    {
        // Pinning is an optimisation, not a requirement: a kernel that refuses it must leave a working
        // executor behind rather than a dead one.
        int fds[2] {-1, -1};
        REQUIRE(::pipe(fds) == 0);

        completion::executor_config config {};
        config.core_id = 0;

        const scoped_fault fault {syscall_id::pthread_setaffinity_np, EINVAL, 1u};

        completion::executor exec {config};
        bool completed = false;
        std::array<char, 8> buffer {};

        auto body = [&exec, &completed, &buffer, fd = fds[0]]() -> task<void>
        {
            const auto r = co_await exec.async_read(fd, std::span<char>(buffer.data(), buffer.size()));
            (void) r;
            completed = true;
            exec.stop();
        };
        exec.spawn(body());

        kmx::aio::test::scoped_completion_runner runner {exec};
        std::this_thread::sleep_for(50ms);

        const char byte = 'x';
        REQUIRE(::write(fds[1], &byte, 1u) == 1);
        REQUIRE(runner.wait_until_drained(std::chrono::seconds(5)));

        CHECK(completed);

        ::close(fds[0]);
        ::close(fds[1]);
    }

    TEST_CASE("a rejected core pin does not stop the readiness loop", "[fault][readiness][affinity]")
    {
        socket_pair sockets;
        REQUIRE(sockets.valid());

        readiness::executor_config config {};
        config.core_id = 0;

        const scoped_fault fault {syscall_id::pthread_setaffinity_np, EINVAL, 1u};

        auto exec = std::make_shared<readiness::executor>(config);
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        std::atomic_bool parked {false};
        std::atomic_bool fired {false};
        std::atomic_bool done {false};

        auto body = [&parked, &fired, &done, exec, fd = sockets.local()]() -> task<void>
        {
            parked.store(true, std::memory_order_release);
            const bool event = co_await exec->wait_io(fd, readiness::event_type::read);
            fired.store(event, std::memory_order_release);
            done.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(parked, 2s));

        const char byte = 'x';
        REQUIRE(::write(sockets.peer(), &byte, 1u) == 1);

        REQUIRE(wait_for_flag(done, 5s));
        CHECK(fired.load(std::memory_order_acquire));
    }

    TEST_CASE("is_io_thread_affined_to forwards a failing query", "[fault][completion][affinity]")
    {
        int fds[2] {-1, -1};
        REQUIRE(::pipe(fds) == 0);

        completion::executor exec;
        std::atomic_bool submitted {false};
        std::array<char, 8> buffer {};

        auto body = [&exec, &submitted, &buffer, fd = fds[0]]() -> task<void>
        {
            submitted.store(true, std::memory_order_release);
            const auto r = co_await exec.async_read(fd, std::span<char>(buffer.data(), buffer.size()));
            (void) r;
        };
        exec.spawn(body());

        kmx::aio::test::scoped_completion_runner runner {exec};
        REQUIRE(wait_for_flag(submitted, 2s));
        REQUIRE(wait_for_io_thread(exec, 3s));

        {
            const scoped_fault fault {syscall_id::pthread_getaffinity_np, ESRCH, 1u};
            const auto result = exec.is_io_thread_affined_to(0);
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error() == std::errc::no_such_process);
        }

        const char byte = 'x';
        (void) ::write(fds[1], &byte, 1u);
        exec.stop();
        REQUIRE(runner.wait_until_drained(std::chrono::seconds(5)));

        ::close(fds[0]);
        ::close(fds[1]);
    }

    TEST_CASE("the readiness affinity query forwards a failure", "[fault][readiness][affinity]")
    {
        socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_shared<readiness::executor>();
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        std::atomic_bool parked {false};
        auto body = [&parked, exec, fd = sockets.local()]() -> task<void>
        {
            parked.store(true, std::memory_order_release);
            const bool event = co_await exec->wait_io(fd, readiness::event_type::read);
            (void) event;
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(parked, 2s));
        REQUIRE(wait_for_io_thread(*exec, 3s));

        {
            const scoped_fault fault {syscall_id::pthread_getaffinity_np, ESRCH, 1u};
            const auto result = exec->is_io_thread_affined_to(0);
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error() == std::errc::no_such_process);
        }

        exec->cancel_io(sockets.local());
    }

    // =========================================================================
    // allocation failures during setup
    // =========================================================================

    TEST_CASE("create_socket reports a failing socket call", "[fault][core][file_descriptor]")
    {
        const scoped_fault fault {syscall_id::socket, EMFILE, 1u};

        const auto created = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE_FALSE(created.has_value());
        CHECK(created.error() == std::errc::too_many_files_open);
    }

    TEST_CASE("a UDP socket propagates a failing socket call", "[fault][completion][udp]")
    {
        // completion::udp::socket::create forwards what file_descriptor::create_socket reports rather
        // than inventing an error of its own, and endpoint::create forwards that in turn. Neither
        // forward had been taken, because nothing else in the suite can make socket() fail.
        completion::executor exec;

        {
            const scoped_fault fault {syscall_id::socket, ENFILE, 1u};
            const auto sock = completion::udp::socket::create(exec, AF_INET);
            REQUIRE_FALSE(sock.has_value());
            CHECK(sock.error() == std::errc::too_many_files_open_in_system);
        }

        {
            const scoped_fault fault {syscall_id::socket, ENFILE, 1u};
            const auto ep = completion::udp::endpoint::create(exec, AF_INET);
            REQUIRE_FALSE(ep.has_value());
            CHECK(ep.error() == std::errc::too_many_files_open_in_system);
        }
    }

    TEST_CASE("a TLS stream releases what it owns when a BIO cannot be created", "[fault][core][tls]")
    {
        // The constructor allocates an SSL and then two memory BIOs. If either BIO fails it has to free
        // the SSL and whichever BIO it did get before throwing - the alternative is leaking an SSL and
        // half a BIO pair on every failed session setup. Running this under the sanitiser build is what
        // turns the assertion below into a real check of that cleanup.
        ::SSL_CTX* const ctx = ::SSL_CTX_new(::TLS_method());
        REQUIRE(ctx != nullptr);

        SECTION("the first BIO fails")
        {
            const scoped_fault fault {syscall_id::bio_new, ENOMEM, 1u};
            CHECK_THROWS_AS((tls::stream<tls_stub_stream> {tls_stub_stream {}, ctx}), std::bad_alloc);
        }

        SECTION("the second BIO fails")
        {
            // Skips the first allocation so the constructor holds one BIO and has to free it too.
            const scoped_fault fault {syscall_id::bio_new, ENOMEM, 1u, /*skip=*/1u};
            CHECK_THROWS_AS((tls::stream<tls_stub_stream> {tls_stub_stream {}, ctx}), std::bad_alloc);
        }

        SECTION("both BIOs fail")
        {
            const scoped_fault fault {syscall_id::bio_new, ENOMEM, 2u};
            CHECK_THROWS_AS((tls::stream<tls_stub_stream> {tls_stub_stream {}, ctx}), std::bad_alloc);
        }

        SECTION("no fault armed")
        {
            // The stub instantiation's destructor only runs on a construction that succeeds, and every
            // section above throws. Without this the type would be reported as having a destructor that
            // nothing ever called - an artefact of this test file rather than of the library.
            {
                const tls::stream<tls_stub_stream> healthy {tls_stub_stream {5}, ctx};
                CHECK(healthy.next_layer() != nullptr);
            }

            SUCCEED("the stub instantiation constructed and released its SSL");
        }

        ::SSL_CTX_free(ctx);
    }

    // =========================================================================
    // shutdown that cannot cancel what is in flight
    // =========================================================================

    TEST_CASE("a shutdown whose cancellation cannot be submitted gives up at the deadline",
              "[fault][completion][executor][shutdown][slow]")
    {
        // The completion loop will not abandon a suspended coroutine: on stop it submits a cancel-all
        // and keeps draining until the work count reaches zero. That drain cannot be unbounded, or one
        // stuck operation would hang shutdown for good, so it is capped by a deadline.
        //
        // Making the cancel-all submission fail exercises both halves at once: the failure is logged and
        // counted, and because nothing was cancelled the parked read never completes, so the drain runs
        // to its deadline and the loop leaves with the count still non-zero. Neither had been reached -
        // a submission that fails at exactly that moment is not something a test can arrange without a
        // seam.
        //
        // Tagged slow: the deadline is five seconds, and waiting it out is the point.
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        completion::executor exec;
        std::atomic_bool submitted {false};
        std::array<char, 8> buffer {};

        auto body = [&exec, &submitted, &buffer, fd = pipes.read_end()]() -> task<void>
        {
            submitted.store(true, std::memory_order_release);

            // Nothing is ever written, so only a cancellation could end this - and the fault below is
            // what stops that cancellation from being submitted.
            const auto r = co_await exec.async_read(fd, std::span<char>(buffer.data(), buffer.size()));
            (void) r;
        };
        exec.spawn(body());

        kmx::aio::test::scoped_completion_runner runner {exec};
        REQUIRE(wait_for_flag(submitted, 2s));
        std::this_thread::sleep_for(50ms);

        const auto errors_before = exec.get_stats().error_count.load();

        {
            // One failure is enough: the loop makes a single cancel-all attempt and then only waits.
            const scoped_fault fault {syscall_id::io_uring_submit, EAGAIN, 1u};
            exec.stop();

            // The drain deadline is five seconds; allow generously more before calling it a hang.
            REQUIRE(runner.wait_until_drained(20s));
        }

        CHECK(exec.get_stats().error_count.load() > errors_before);
    }
}

#endif // KMX_AIO_FAULT_INJECTION
