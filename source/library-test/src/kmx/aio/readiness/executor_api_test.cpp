/// @file aio/readiness/executor_api_test.cpp
/// @brief Unit tests for the readiness executor's backend selection, operation surface and accessors.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <system_error>
#include <thread>

#include <expected>
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <unistd.h>

#include <kmx/aio/error_code.hpp>
#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/readiness/openonload/extensions.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/aio/test/executor_runner.hpp>

namespace kmx::aio::test::readiness::executor_api_test
{
    using namespace kmx::aio::readiness;

    using namespace std::literals::chrono_literals;
    using kmx::aio::test::scoped_runner;
    using kmx::aio::test::wait_for_flag;

    namespace detail
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

        /// @brief What one asynchronous operation reported.
        struct outcome
        {
            std::atomic_bool completed {false};
            std::atomic_bool ok {false};
            std::atomic_size_t value {0u};
            std::error_code error {};
        };
    } // namespace detail

    // statistics
    TEST_CASE("statistics::reset zeroes every counter", "[readiness][executor][statistics]")
    {
        statistics stats;
        stats.total_registrations.store(1u);
        stats.total_unregistrations.store(2u);
        stats.total_epoll_waits.store(3u);
        stats.total_events_received.store(4u);
        stats.timeout_count.store(5u);
        stats.error_count.store(6u);
        stats.total_tasks_spawned.store(7u);
        stats.total_tasks_completed.store(8u);

        stats.reset();

        CHECK(stats.total_registrations.load() == 0u);
        CHECK(stats.total_unregistrations.load() == 0u);
        CHECK(stats.total_epoll_waits.load() == 0u);
        CHECK(stats.total_events_received.load() == 0u);
        CHECK(stats.timeout_count.load() == 0u);
        CHECK(stats.error_count.load() == 0u);
        CHECK(stats.total_tasks_spawned.load() == 0u);
        CHECK(stats.total_tasks_completed.load() == 0u);
    }

    TEST_CASE("reset_stats clears counters the executor accumulated", "[readiness][executor][statistics]")
    {
        detail::socket_pair sockets;
        REQUIRE(sockets.valid());

        executor exec;
        REQUIRE(exec.register_fd(sockets.local()).has_value());
        REQUIRE(exec.get_stats().total_registrations.load() > 0u);

        exec.reset_stats();
        CHECK(exec.get_stats().total_registrations.load() == 0u);
        CHECK(exec.get_stats().total_unregistrations.load() == 0u);
    }

    TEST_CASE("register_fd and unregister_fd move the counters", "[readiness][executor][registration]")
    {
        detail::socket_pair sockets;
        REQUIRE(sockets.valid());

        executor exec;
        REQUIRE(exec.register_fd(sockets.local()).has_value());
        CHECK(exec.get_stats().total_registrations.load() == 1u);

        exec.unregister_fd(sockets.local());
        CHECK(exec.get_stats().total_unregistrations.load() == 1u);
    }

    TEST_CASE("register_fd reports a bad descriptor and counts the error", "[readiness][executor][registration][error]")
    {
        executor exec;
        const auto result = exec.register_fd(-1);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::bad_file_descriptor);
        CHECK(exec.get_stats().error_count.load() > 0u);
    }

    TEST_CASE("unregister_fd of an unknown descriptor is harmless", "[readiness][executor][registration]")
    {
        detail::socket_pair sockets;
        REQUIRE(sockets.valid());

        executor exec;
        exec.unregister_fd(sockets.local());
        exec.unregister_fd(-1);
        SUCCEED("unregistering a descriptor that was never registered does not fault");
    }

    // backend selection
    TEST_CASE("epoll_only selects the epoll backend", "[readiness][executor][backend]")
    {
        const executor_config config {.backend = backend_mode::epoll_only};
        executor exec {config};
        CHECK(exec.get_active_backend() == active_backend::epoll);
    }

    namespace detail
    {
        /// @brief Sets an environment variable for the duration of a test and restores it after.
        /// @details The OpenOnload runtime is detected purely from the environment, which is how a test
        ///          can drive the accelerated selection path on a machine that has no Onload at all.
        ///          The variable is process-wide, so it has to be put back however the test ends.
        class scoped_env
        {
        public:
            scoped_env(const char* const name, const char* const value) noexcept: name_(name)
            {
                if (const char* const previous = ::getenv(name))
                {
                    previous_ = previous;
                    had_previous_ = true;
                }

                ::setenv(name, value, 1);
            }

            scoped_env(const scoped_env&) = delete;
            scoped_env& operator=(const scoped_env&) = delete;

            ~scoped_env() noexcept
            {
                if (had_previous_)
                    ::setenv(name_, previous_.c_str(), 1);
                else
                    ::unsetenv(name_);
            }

        private:
            const char* name_;
            std::string previous_ {};
            bool had_previous_ = false;
        };

        /// @brief True when the library was built with the OpenOnload detection compiled in.
        [[nodiscard]] constexpr bool openonload_detection_compiled() noexcept
        {
#if defined(KMX_AIO_FEATURE_OPENONLOAD)
            return true;
#else
            return false;
#endif
        }
    } // namespace detail

    TEST_CASE("ONLOAD_STACKNAME selects the accelerated backend", "[readiness][executor][backend][openonload]")
    {
        // Detection is environment-only: the executor never dlopens or probes hardware, so setting the
        // hint is enough to drive the accelerated selection arm on a machine with no Onload installed.
        if constexpr (!detail::openonload_detection_compiled())
        {
            SUCCEED("built without KMX_AIO_FEATURE_OPENONLOAD; detection is a constexpr false");
            return;
        }

        const detail::scoped_env stackname {"ONLOAD_STACKNAME", "kmxaio_test"};

        const executor_config config {.backend = backend_mode::openonload_preferred};
        executor exec {config};
        CHECK(exec.get_active_backend() == active_backend::openonload);
    }

    TEST_CASE("EF_POLL_USEC selects the accelerated backend", "[readiness][executor][backend][openonload]")
    {
        if constexpr (!detail::openonload_detection_compiled())
        {
            SUCCEED("built without KMX_AIO_FEATURE_OPENONLOAD");
            return;
        }

        const detail::scoped_env poll_usec {"EF_POLL_USEC", "50"};

        const executor_config config {.backend = backend_mode::openonload_preferred};
        executor exec {config};
        CHECK(exec.get_active_backend() == active_backend::openonload);
    }

    TEST_CASE("an onload LD_PRELOAD selects the accelerated backend", "[readiness][executor][backend][openonload]")
    {
        // This is the substring arm rather than a plain presence check: LD_PRELOAD routinely carries
        // several libraries, and only one of them being Onload is what counts.
        if constexpr (!detail::openonload_detection_compiled())
        {
            SUCCEED("built without KMX_AIO_FEATURE_OPENONLOAD");
            return;
        }

        const detail::scoped_env preload {"LD_PRELOAD", "/usr/lib/libsomething.so:/usr/lib/libonload.so"};

        const executor_config config {.backend = backend_mode::openonload_preferred};
        executor exec {config};
        CHECK(exec.get_active_backend() == active_backend::openonload);
    }

    TEST_CASE("an LD_PRELOAD without onload does not select the accelerated backend", "[readiness][executor][backend][openonload]")
    {
        // The negative half of the substring check: an unrelated preload must not be mistaken for Onload.
        const detail::scoped_env preload {"LD_PRELOAD", "/usr/lib/libsomething.so"};

        const executor_config config {.backend = backend_mode::openonload_preferred};
        executor exec {config};
        CHECK(exec.get_active_backend() == active_backend::epoll);
    }

    TEST_CASE("openonload_required is satisfied when the runtime is advertised", "[readiness][executor][backend][openonload]")
    {
        if constexpr (!detail::openonload_detection_compiled())
        {
            SUCCEED("built without KMX_AIO_FEATURE_OPENONLOAD");
            return;
        }

        const detail::scoped_env stackname {"ONLOAD_STACKNAME", "kmxaio_test"};

        const executor_config config {.backend = backend_mode::openonload_required};
        executor exec {config};
        CHECK(exec.get_active_backend() == active_backend::openonload);
    }

    TEST_CASE("openonload_preferred falls back to epoll when the runtime is absent", "[readiness][executor][backend]")
    {
        // "preferred" has to degrade rather than fail when no runtime is advertised: that fallback is
        // the whole difference between preferred and required. The hints are cleared explicitly so the
        // test does not depend on the shell it was launched from.
        const detail::scoped_env stackname {"ONLOAD_STACKNAME", ""};
        const detail::scoped_env poll_usec {"EF_POLL_USEC", ""};
        const detail::scoped_env preload {"LD_PRELOAD", ""};
        ::unsetenv("ONLOAD_STACKNAME");
        ::unsetenv("EF_POLL_USEC");

        const executor_config config {.backend = backend_mode::openonload_preferred};
        executor exec {config};
        CHECK(exec.get_active_backend() == active_backend::epoll);
    }

    TEST_CASE("openonload_required fails construction when the runtime is absent", "[readiness][executor][backend][error]")
    {
        const detail::scoped_env stackname {"ONLOAD_STACKNAME", ""};
        const detail::scoped_env poll_usec {"EF_POLL_USEC", ""};
        const detail::scoped_env preload {"LD_PRELOAD", ""};
        ::unsetenv("ONLOAD_STACKNAME");
        ::unsetenv("EF_POLL_USEC");

        const executor_config config {.backend = backend_mode::openonload_required};

        try
        {
            executor exec {config};
            // Reached only on a host that really does have OpenOnload, where the requirement is met.
            CHECK(exec.get_active_backend() == active_backend::openonload);
        }
        catch (const std::system_error& error)
        {
            CHECK(error.code() == aio::to_std_error_code(aio::error_code::openonload_not_available));
        }
    }

    // async_recvmsg / async_sendmsg / async_timeout
    TEST_CASE("async_sendmsg and async_recvmsg move a datagram", "[readiness][executor][msg]")
    {
        detail::socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_shared<executor>();
        REQUIRE(exec->register_fd(sockets.local()).has_value());
        REQUIRE(exec->register_fd(sockets.peer()).has_value());

        detail::outcome sent;
        detail::outcome received;
        std::string payload {"readiness"};
        std::array<char, 64> buffer {};

        auto body = [&sent, &received, &payload, &buffer, exec, &sockets]() -> task<void>
        {
            ::iovec out_iov {payload.data(), payload.size()};
            ::msghdr out {};
            out.msg_iov = &out_iov;
            out.msg_iovlen = 1u;

            const auto s = co_await exec->async_sendmsg(sockets.peer(), &out);
            sent.completed.store(true, std::memory_order_release);
            if (s)
            {
                sent.ok.store(true, std::memory_order_release);
                sent.value.store(*s, std::memory_order_release);
            }
            else
                sent.error = s.error();

            ::iovec in_iov {buffer.data(), buffer.size()};
            ::msghdr in {};
            in.msg_iov = &in_iov;
            in.msg_iovlen = 1u;

            const auto r = co_await exec->async_recvmsg(sockets.local(), &in);
            received.completed.store(true, std::memory_order_release);
            if (r)
            {
                received.ok.store(true, std::memory_order_release);
                received.value.store(*r, std::memory_order_release);
            }
            else
                received.error = r.error();
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(received.completed, 5s));

        CHECK(sent.ok.load(std::memory_order_acquire));
        CHECK(sent.value.load(std::memory_order_acquire) == payload.size());
        CHECK(received.ok.load(std::memory_order_acquire));
        CHECK(received.value.load(std::memory_order_acquire) == payload.size());
        CHECK(std::string(buffer.data(), received.value.load(std::memory_order_acquire)) == payload);
    }

    TEST_CASE("async_recvmsg parks until the peer writes", "[readiness][executor][msg]")
    {
        // The socket is empty when the receive starts, so the first recvmsg returns EAGAIN and the
        // coroutine parks on the executor - the readiness path proper, rather than the fast path.
        detail::socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_shared<executor>();
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        detail::outcome received;
        std::atomic_bool started {false};
        std::array<char, 64> buffer {};

        auto body = [&received, &started, &buffer, exec, fd = sockets.local()]() -> task<void>
        {
            ::iovec iov {buffer.data(), buffer.size()};
            ::msghdr msg {};
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1u;

            started.store(true, std::memory_order_release);
            const auto r = co_await exec->async_recvmsg(fd, &msg);
            if (r)
            {
                received.ok.store(true, std::memory_order_release);
                received.value.store(*r, std::memory_order_release);
            }
            else
                received.error = r.error();

            received.completed.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(started, 2s));
        std::this_thread::sleep_for(50ms);

        const std::string payload {"late"};
        REQUIRE(::write(sockets.peer(), payload.data(), payload.size()) == static_cast<ssize_t>(payload.size()));

        REQUIRE(wait_for_flag(received.completed, 5s));
        CHECK(received.ok.load(std::memory_order_acquire));
        CHECK(received.value.load(std::memory_order_acquire) == payload.size());
    }

    TEST_CASE("async_sendmsg reports a bad descriptor", "[readiness][executor][msg][error]")
    {
        auto exec = std::make_shared<executor>();

        detail::outcome sent;
        std::string payload {"x"};

        auto body = [&sent, &payload, exec]() -> task<void>
        {
            ::iovec iov {payload.data(), payload.size()};
            ::msghdr msg {};
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1u;

            const auto s = co_await exec->async_sendmsg(-1, &msg);
            if (s)
                sent.ok.store(true, std::memory_order_release);
            else
                sent.error = s.error();

            sent.completed.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(sent.completed, 5s));
        CHECK_FALSE(sent.ok.load(std::memory_order_acquire));
        CHECK(sent.error == std::errc::bad_file_descriptor);
    }

    TEST_CASE("async_timeout completes after the requested delay", "[readiness][executor][timeout]")
    {
        auto exec = std::make_shared<executor>();

        detail::outcome waited;
        std::chrono::steady_clock::time_point start {};
        std::chrono::steady_clock::time_point end {};

        auto body = [&waited, &start, &end, exec]() -> task<void>
        {
            start = std::chrono::steady_clock::now();
            const auto t = co_await exec->async_timeout(30'000'000u); // 30ms
            end = std::chrono::steady_clock::now();
            if (t)
                waited.ok.store(true, std::memory_order_release);
            else
                waited.error = t.error();

            waited.completed.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(waited.completed, 5s));
        CHECK(waited.ok.load(std::memory_order_acquire));
        CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() >= 25);
    }

    // lifecycle accessors
    TEST_CASE("is_io_thread_affined_to rejects a negative core", "[readiness][executor][affinity][error]")
    {
        executor exec;
        const auto result = exec.is_io_thread_affined_to(-1);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::invalid_argument);
    }

    TEST_CASE("is_io_thread_affined_to refuses a stopped executor", "[readiness][executor][affinity][error]")
    {
        executor exec;
        const auto result = exec.is_io_thread_affined_to(0);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::operation_not_permitted);
    }

    TEST_CASE("stop on an executor that never ran is harmless", "[readiness][executor][lifecycle]")
    {
        executor exec;
        exec.stop();
        exec.stop();
        SUCCEED("stop() is safe to call on an executor that was never run");
    }

    TEST_CASE("the lifetime token expires with the executor", "[readiness][executor][lifecycle]")
    {
        std::weak_ptr<void> token;
        {
            executor exec;
            token = exec.get_lifetime_token();
            CHECK_FALSE(token.expired());
        }

        CHECK(token.expired());
    }

    // OpenOnload shims, in their without-the-runtime form
    TEST_CASE("the OpenOnload shims degrade without the runtime", "[readiness][openonload]")
    {
        // Built without the vendor headers these are the fallback definitions, and what they promise is
        // that a caller can ask and be told no rather than fail to link or crash.
        CHECK_FALSE(openonload::initialize_runtime_stack("kmxaio_test_stack"));
        CHECK_FALSE(openonload::is_accelerated_fd(0));

        std::array<char, 8> buffer {};
        const auto received = openonload::zero_copy_receive(0, std::span<char>(buffer.data(), buffer.size()));
        REQUIRE_FALSE(received.has_value());
        CHECK(received.error() == std::errc::function_not_supported);

        const auto sent = openonload::zero_copy_send(0, cspan_char_t(buffer.data(), buffer.size()));
        REQUIRE_FALSE(sent.has_value());
        CHECK(sent.error() == std::errc::function_not_supported);
    }

    // core pinning
    namespace detail
    {
        /// @brief The first CPU this thread is allowed to run on.
        /// @details Pinning to a core outside the process's own affinity mask fails, and on a machine
        ///          under cgroup or taskset restrictions core 0 need not be in it.
        [[nodiscard]] expected_int_t first_allowed_cpu() noexcept
        {
            cpu_set_t allowed {};
            CPU_ZERO(&allowed);

            const int ret = ::pthread_getaffinity_np(::pthread_self(), sizeof(cpu_set_t), &allowed);
            if (ret != 0)
                return std::unexpected(std::error_code(ret, std::generic_category()));

            for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
                if (CPU_ISSET(cpu, &allowed) != 0)
                    return cpu;

            return std::unexpected(std::make_error_code(std::errc::no_such_device));
        }
    } // namespace detail

    TEST_CASE("a configured core pins the I/O thread", "[readiness][executor][affinity]")
    {
        const auto core = detail::first_allowed_cpu();
        REQUIRE(core.has_value());

        detail::socket_pair sockets;
        REQUIRE(sockets.valid());

        executor_config config {};
        config.core_id = static_cast<std::int16_t>(*core);

        auto exec = std::make_shared<executor>(config);
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        // A wait that nothing will satisfy keeps the loop alive long enough to be asked about; the
        // cancel at the end of the test releases it.
        std::atomic_bool parked {false};
        auto body = [&parked, exec, fd = sockets.local()]() -> task<void>
        {
            parked.store(true, std::memory_order_release);
            const bool fired = co_await exec->wait_io(fd, event_type::read);
            (void) fired;
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(parked, 2s));

        expected_bool_t affined = std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            affined = exec->is_io_thread_affined_to(*core);
            if (affined.has_value())
                break;

            std::this_thread::sleep_for(5ms);
        }

        const bool observed = affined.has_value() && *affined;
        exec->cancel_io(sockets.local());

        CHECK(observed);
    }

    TEST_CASE("a negative core leaves the I/O thread unpinned", "[readiness][executor][affinity]")
    {
        detail::socket_pair sockets;
        REQUIRE(sockets.valid());

        executor_config config {};
        config.core_id = -1;

        auto exec = std::make_shared<executor>(config);
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        std::atomic_bool parked {false};
        auto body = [&parked, exec, fd = sockets.local()]() -> task<void>
        {
            parked.store(true, std::memory_order_release);
            const bool fired = co_await exec->wait_io(fd, event_type::read);
            (void) fired;
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(parked, 2s));
        std::this_thread::sleep_for(50ms);

        const auto core = detail::first_allowed_cpu();
        REQUIRE(core.has_value());
        const auto affined = exec->is_io_thread_affined_to(*core);

        exec->cancel_io(sockets.local());

        // Unpinned means the thread still carries the whole process mask, which includes this core.
        if (affined.has_value())
            CHECK(*affined);
    }

    TEST_CASE("cancel_io leaves waits on other descriptors alone", "[readiness][executor][cancellation]")
    {
        // cancel_waiters walks every subscription and has to skip the ones belonging to a different
        // descriptor rather than resuming the lot.
        detail::socket_pair first;
        detail::socket_pair second;
        REQUIRE(first.valid());
        REQUIRE(second.valid());

        auto exec = std::make_shared<executor>();
        REQUIRE(exec->register_fd(first.local()).has_value());
        REQUIRE(exec->register_fd(second.local()).has_value());

        std::atomic_bool first_parked {false};
        std::atomic_bool second_parked {false};
        std::atomic_bool first_done {false};
        std::atomic_bool second_done {false};
        std::atomic_bool first_fired {false};
        std::atomic_bool second_fired {false};

        auto first_body = [&first_parked, &first_done, &first_fired, exec, fd = first.local()]() -> task<void>
        {
            first_parked.store(true, std::memory_order_release);
            const bool fired = co_await exec->wait_io(fd, event_type::read);
            first_fired.store(fired, std::memory_order_release);
            first_done.store(true, std::memory_order_release);
        };

        auto second_body = [&second_parked, &second_done, &second_fired, exec, fd = second.local()]() -> task<void>
        {
            second_parked.store(true, std::memory_order_release);
            const bool fired = co_await exec->wait_io(fd, event_type::read);
            second_fired.store(fired, std::memory_order_release);
            second_done.store(true, std::memory_order_release);
        };

        exec->spawn(first_body());
        exec->spawn(second_body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(first_parked, 2s));
        REQUIRE(wait_for_flag(second_parked, 2s));
        std::this_thread::sleep_for(50ms);

        exec->cancel_io(first.local());
        REQUIRE(wait_for_flag(first_done, 5s));
        CHECK_FALSE(first_fired.load(std::memory_order_acquire));

        // The second wait must still be parked: a real event, not the cancel, is what ends it.
        CHECK_FALSE(second_done.load(std::memory_order_acquire));

        const char byte = 'x';
        REQUIRE(::write(second.peer(), &byte, 1u) == 1);
        REQUIRE(wait_for_flag(second_done, 5s));
        CHECK(second_fired.load(std::memory_order_acquire));
    }

    TEST_CASE("stop wakes an idle event loop instead of waiting for its timeout", "[readiness][executor][lifecycle]")
    {
        // An idle loop is parked inside epoll_wait for timeout_ms at a time, and a stop request changes
        // nothing it is waiting on. Without something to wake it, a shutdown costs whatever is left of
        // the current wait - here five seconds, which is the whole point of choosing a long one.
        const executor_config config {.thread_count = 1u, .timeout_ms = 5000u};
        auto exec = std::make_shared<executor>(config);

        std::jthread runner {[exec]() { exec->run(); }};

        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while ((exec->get_stats().total_epoll_waits.load(std::memory_order_relaxed) == 0u) && (std::chrono::steady_clock::now() < deadline))
            std::this_thread::sleep_for(1ms);

        REQUIRE(exec->get_stats().total_epoll_waits.load(std::memory_order_relaxed) > 0u);
        std::this_thread::sleep_for(20ms);

        const auto started = std::chrono::steady_clock::now();
        exec->stop();
        const auto elapsed = std::chrono::steady_clock::now() - started;

        // A wide margin on purpose: what is being distinguished is "woken" from "waited out a
        // five-second timer", and no amount of load on the test machine blurs that.
        CHECK(elapsed < 2s);
    }

    // Where a ready coroutine continues
    /// @brief Records the threads a parked coroutine started on and woke up on.
    struct wake_up_record
    {
        std::atomic<std::thread::id> started_on {};
        std::atomic<std::thread::id> resumed_on {};
        std::atomic_bool parked {false};
        std::atomic_bool done {false};
    };

    TEST_CASE("the default executor resumes a wait on a scheduler worker", "[readiness][executor][resumption]")
    {
        // The default, and the reference point for the inline case below: with a single worker, the
        // coroutine starts and resumes on that same worker, and the I/O thread only hands the
        // resumption over.
        detail::socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_shared<executor>(executor_config {.thread_count = 1u, .timeout_ms = 20u});
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        wake_up_record record;
        auto body = [&record, exec, fd = sockets.local()]() -> task<void>
        {
            record.started_on.store(std::this_thread::get_id(), std::memory_order_release);
            record.parked.store(true, std::memory_order_release);
            const bool fired = co_await exec->wait_io(fd, event_type::read);
            CHECK(fired);
            record.resumed_on.store(std::this_thread::get_id(), std::memory_order_release);
            record.done.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(record.parked, 2s));
        std::this_thread::sleep_for(50ms);

        const char byte = 'x';
        REQUIRE(::write(sockets.peer(), &byte, 1u) == 1);
        REQUIRE(wait_for_flag(record.done, 5s));

        CHECK(record.resumed_on.load(std::memory_order_acquire) == record.started_on.load(std::memory_order_acquire));
        CHECK(record.resumed_on.load(std::memory_order_acquire) != std::this_thread::get_id());
    }

    TEST_CASE("an inline executor resumes a wait on its own I/O thread", "[readiness][executor][resumption]")
    {
        // The thread-per-core arrangement: the event is observed on the I/O thread and the coroutine
        // continues there, so the wake-up lands on a thread that is neither the one that started the
        // task - a scheduler worker, because spawn() still goes through the scheduler - nor the test's.
        detail::socket_pair sockets;
        REQUIRE(sockets.valid());

        const executor_config config {.thread_count = 1u, .timeout_ms = 20u, .resumption = resumption_mode::inline_on_io_thread};
        auto exec = std::make_shared<executor>(config);
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        wake_up_record record;
        auto body = [&record, exec, fd = sockets.local()]() -> task<void>
        {
            record.started_on.store(std::this_thread::get_id(), std::memory_order_release);
            record.parked.store(true, std::memory_order_release);
            const bool fired = co_await exec->wait_io(fd, event_type::read);
            CHECK(fired);
            record.resumed_on.store(std::this_thread::get_id(), std::memory_order_release);
            record.done.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(record.parked, 2s));
        std::this_thread::sleep_for(50ms);

        const char byte = 'x';
        REQUIRE(::write(sockets.peer(), &byte, 1u) == 1);
        REQUIRE(wait_for_flag(record.done, 5s));

        CHECK(record.resumed_on.load(std::memory_order_acquire) != record.started_on.load(std::memory_order_acquire));
        CHECK(record.resumed_on.load(std::memory_order_acquire) != std::this_thread::get_id());
    }

    TEST_CASE("an inline executor still cancels a wait from another thread", "[readiness][executor][resumption][cancellation]")
    {
        // A cancellation arrives on whatever thread called cancel_io(), which is not the I/O thread and
        // must not run the coroutine. The waiter is handed to the scheduler instead, and what matters
        // is that it is resumed at all - a cancellation that took the inline path would either run
        // application code on the caller's thread or, worse, not run it.
        detail::socket_pair sockets;
        REQUIRE(sockets.valid());

        const executor_config config {.thread_count = 1u, .timeout_ms = 20u, .resumption = resumption_mode::inline_on_io_thread};
        auto exec = std::make_shared<executor>(config);
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        std::atomic_bool parked {false};
        std::atomic_bool done {false};
        std::atomic_bool fired {true};

        auto body = [&parked, &done, &fired, exec, fd = sockets.local()]() -> task<void>
        {
            parked.store(true, std::memory_order_release);
            fired.store(co_await exec->wait_io(fd, event_type::read), std::memory_order_release);
            done.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(parked, 2s));
        std::this_thread::sleep_for(50ms);

        exec->cancel_io(sockets.local());
        REQUIRE(wait_for_flag(done, 5s));
        CHECK_FALSE(fired.load(std::memory_order_acquire));
    }

    TEST_CASE("an inline executor carries a full exchange", "[readiness][executor][resumption]")
    {
        // Two coroutines on one executor, each waiting on its own end of a socket pair, taking turns.
        // Every resumption after the first happens inside the event loop, so this is the arrangement
        // running under the loop rather than a single wake-up observed from outside it.
        detail::socket_pair sockets;
        REQUIRE(sockets.valid());

        const executor_config config {.thread_count = 1u, .timeout_ms = 20u, .resumption = resumption_mode::inline_on_io_thread};
        auto exec = std::make_shared<executor>(config);
        REQUIRE(exec->register_fd(sockets.local()).has_value());
        REQUIRE(exec->register_fd(sockets.peer()).has_value());

        constexpr int exchanges = 16;
        std::atomic_int echoed {0};
        std::atomic_bool client_done {false};

        const auto read_one = [](executor& e, const int fd) -> task<bool>
        {
            char byte {};
            while (true)
            {
                const auto n = ::read(fd, &byte, 1u);
                if (n == 1)
                    co_return true;

                if ((n == 0) || ((errno != EAGAIN) && (errno != EWOULDBLOCK) && (errno != EINTR)))
                    co_return false;

                if ((errno != EINTR) && !co_await e.wait_io(fd, event_type::read))
                    co_return false;
            }
        };

        auto echo_body = [&echoed, &read_one, exec, fd = sockets.peer()]() -> task<void>
        {
            for (int i = 0; i < exchanges; ++i)
            {
                if (!co_await read_one(*exec, fd))
                    co_return;

                const char byte = 'r';
                if (::write(fd, &byte, 1u) != 1)
                    co_return;

                echoed.fetch_add(1, std::memory_order_acq_rel);
            }
        };

        auto client_body = [&client_done, &read_one, exec, fd = sockets.local()]() -> task<void>
        {
            for (int i = 0; i < exchanges; ++i)
            {
                const char byte = 'q';
                if (::write(fd, &byte, 1u) != 1)
                    co_return;

                if (!co_await read_one(*exec, fd))
                    co_return;
            }

            client_done.store(true, std::memory_order_release);
        };

        exec->spawn(echo_body());
        exec->spawn(client_body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(client_done, 10s));
        CHECK(echoed.load(std::memory_order_acquire) == exchanges);
    }
} // namespace kmx::aio::test::readiness::executor_api_test
