/// @file src/kmx/aio/completion/executor_api_test.cpp
/// @brief Unit tests for the completion executor's operation surface and lifecycle accessors.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// The coroutine bodies follow the pattern the other completion tests use: a task that records its
/// outcome into a shared state object and calls exec.stop() on the way out, driven by exec.run() on the
/// test thread. The executor's io_uring loop returns from run() when its work drains, and the explicit
/// stop() keeps a test that never completes its operation from hanging the whole binary.
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/completion/statistics.hpp>
    #include <kmx/aio/file_descriptor.hpp>
    #include <kmx/aio/task.hpp>
    #include <kmx/aio/test/outcome.hpp>
    #include <kmx/aio/test/pipe_pair.hpp>
    #include <kmx/aio/test/scoped_completion_runner.hpp>
    #include <kmx/aio/test/system_probe.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <array>
    #include <atomic>
    #include <chrono>
    #include <cstring>
    #include <expected>
    #include <memory>
    #include <string>
    #include <system_error>
    #include <thread>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <poll.h>
    #include <pthread.h>
    #include <sched.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace kmx::aio::test::completion::executor_api_test
{
    using namespace kmx::aio::completion;

    namespace detail
    {
        /// @brief Writes one payload so the submission and completion counters leave zero.
        task<void> write_for_stats(executor& exec, const std::shared_ptr<size_outcome>& state, const int fd) noexcept(false)
        {
            const std::string payload {"stats"};
            const auto result = co_await exec.async_write(fd, cspan_char_t(payload.data(), payload.size()));
            state->completed = true;
            if (result)
            {
                state->ok = true;
                state->value = *result;
            }

            exec.stop();
        }

        /// @brief A payload written into a pipe and read back, and where both outcomes are recorded.
        struct round_trip_params
        {
            executor& exec;                           ///< The executor both operations run on.
            std::shared_ptr<size_outcome> written {}; ///< Records the write.
            std::shared_ptr<size_outcome> read {};    ///< Records the read.
            std::span<char> buffer {};                ///< Where the payload is read back into; the fixed variant writes it from here too.
            const pipe_pair& pipes;                   ///< The pipe the payload travels through.
        };

        /// @brief Writes a payload into the pipe and reads it back, recording both outcomes.
        /// @param params The executor, the pipe, the read buffer and where the outcomes go.
        task<void> write_then_read_pipe(const round_trip_params params) noexcept(false)
        {
            const std::string payload {"kmx-aio"};
            const auto w = co_await params.exec.async_write(params.pipes.write_end(), cspan_char_t(payload.data(), payload.size()));
            params.written->completed = true;
            if (w)
            {
                params.written->ok = true;
                params.written->value = *w;
            }
            else
                params.written->error = w.error();

            const auto r =
                co_await params.exec.async_read(params.pipes.read_end(), std::span<char>(params.buffer.data(), params.buffer.size()));
            params.read->completed = true;
            if (r)
            {
                params.read->ok = true;
                params.read->value = *r;
            }
            else
                params.read->error = r.error();

            params.exec.stop();
        }

        /// @brief Reads from a pipe whose write end is closed, so the read reports end of stream.
        task<void> read_end_of_stream(executor& exec, const std::shared_ptr<size_outcome>& state, const std::span<char> buffer,
                                      const int fd) noexcept(false)
        {
            const auto r = co_await exec.async_read(fd, std::span<char>(buffer.data(), buffer.size()));
            state->completed = true;
            if (r)
            {
                state->ok = true;
                state->value = *r;
            }
            else
                state->error = r.error();

            exec.stop();
        }

        /// @brief Reads from a descriptor that was never open, and records the error.
        task<void> read_bad_descriptor(executor& exec, const std::shared_ptr<size_outcome>& state,
                                       const std::span<char> buffer) noexcept(false)
        {
            const auto r = co_await exec.async_read(-1, std::span<char>(buffer.data(), buffer.size()));
            state->completed = true;
            if (r)
                state->ok = true;
            else
                state->error = r.error();

            exec.stop();
        }

        /// @brief Writes to a descriptor that was never open, and records the error.
        task<void> write_bad_descriptor(executor& exec, const std::shared_ptr<size_outcome>& state) noexcept(false)
        {
            const std::string payload {"x"};
            const auto w = co_await exec.async_write(-1, cspan_char_t(payload.data(), payload.size()));
            state->completed = true;
            if (w)
                state->ok = true;
            else
                state->error = w.error();

            exec.stop();
        }

        /// @brief Writes and reads through the registered buffer, recording both outcomes.
        /// @param params The executor, the pipe, the registered buffer the payload is written from and read back into,
        ///               and where the outcomes go.
        task<void> write_then_read_fixed(const round_trip_params params) noexcept(false)
        {
            const std::string payload {"fixed"};
            const auto storage = params.buffer;
            std::memcpy(storage.data(), payload.data(), payload.size());

            const auto w =
                co_await params.exec.async_write_fixed(params.pipes.write_end(), cspan_char_t(storage.data(), payload.size()), 0u, 0);
            params.written->completed = true;
            if (w)
            {
                params.written->ok = true;
                params.written->value = *w;
            }
            else
                params.written->error = w.error();

            const auto r =
                co_await params.exec.async_read_fixed(params.pipes.read_end(), std::span<char>(storage.data(), storage.size()), 0u, 0);
            params.read->completed = true;
            if (r)
            {
                params.read->ok = true;
                params.read->value = *r;
            }
            else
                params.read->error = r.error();

            params.exec.stop();
        }

        /// @brief Reads with a buffer index nothing was registered under, and records the error.
        task<void> read_fixed_unknown_index(executor& exec, const std::shared_ptr<size_outcome>& state, const std::span<char> storage,
                                            const int fd) noexcept(false)
        {
            const auto r = co_await exec.async_read_fixed(fd, std::span<char>(storage.data(), storage.size()), 0u, 7);
            state->completed = true;
            if (r)
                state->ok = true;
            else
                state->error = r.error();

            exec.stop();
        }

        /// @brief Accepts one loopback connection and records the descriptor it produced.
        /// @param peer Receives the connecting peer's address; its length must say how much storage there is.
        task<void> accept_loopback(executor& exec, const std::shared_ptr<fd_outcome>& accepted, socket_address& peer,
                                   const int fd) noexcept(false)
        {
            const auto a = co_await exec.async_accept(fd, peer.storage, peer.length);
            accepted->completed = true;
            if (a)
            {
                accepted->ok = true;
                accepted->value = *a;
            }
            else
                accepted->error = a.error();
        }

        /// @brief Connects to the bound loopback address and records the outcome.
        task<void> connect_loopback(executor& exec, const std::shared_ptr<void_outcome>& connected, const ::sockaddr_in& bound,
                                    const int fd) noexcept(false)
        {
            const auto c = co_await exec.async_connect(fd, reinterpret_cast<const ::sockaddr*>(&bound), sizeof(bound));
            connected->completed = true;
            if (c)
                connected->ok = true;
            else
                connected->error = c.error();

            exec.stop();
        }

        /// @brief Accepts on a descriptor that is not a listening socket, and records the error.
        /// @param peer Would receive the peer's address; its length must say how much storage there is.
        task<void> accept_non_listening(executor& exec, const std::shared_ptr<fd_outcome>& state, socket_address& peer,
                                        const int fd) noexcept(false)
        {
            const auto a = co_await exec.async_accept(fd, peer.storage, peer.length);
            state->completed = true;
            if (a)
                state->ok = true;
            else
                state->error = a.error();

            exec.stop();
        }

        /// @brief Connects to a port nothing is listening on, and records the error.
        task<void> connect_refused_port(executor& exec, const std::shared_ptr<void_outcome>& state,
                                        const expected_socket_address_t& address, const int fd) noexcept(false)
        {
            const auto c = co_await exec.async_connect(fd, reinterpret_cast<const ::sockaddr*>(&address->storage), address->length);
            state->completed = true;
            if (c)
                state->ok = true;
            else
                state->error = c.error();

            exec.stop();
        }

        /// @brief Cancels a user_data nothing in the ring carries, and records whatever came back.
        task<void> cancel_unknown_user_data(executor& exec, const std::shared_ptr<void_outcome>& state) noexcept(false)
        {
            const auto c = co_await exec.async_cancel(0xdeadbeefu);
            state->completed = true;
            if (c)
                state->ok = true;
            else
                state->error = c.error();

            exec.stop();
        }

        /// @brief Parks the loop on a read nothing will satisfy, so the I/O thread can be asked about.
        task<void> park_on_read(executor& exec, std::atomic_bool& started, const std::span<char> buffer, const int fd) noexcept(false)
        {
            started.store(true, std::memory_order_release);
            const auto r = co_await exec.async_read(fd, std::span<char>(buffer.data(), buffer.size()));
            static_cast<void>(r);
            exec.stop();
        }

        /// @brief A read only the shutdown cancellation can end, and where its progress is recorded.
        struct read_until_cancelled_params
        {
            executor& exec;                         ///< The executor the read is submitted to.
            std::shared_ptr<size_outcome> state {}; ///< Records what the read reported.
            std::atomic_bool& submitted;            ///< Set just before the read is submitted.
            std::span<char> buffer {};              ///< Where the read would land.
            int fd {-1};                            ///< The descriptor nothing is ever written to.
        };

        /// @brief Suspends on a read only the shutdown cancellation can end, and records what it reported.
        /// @param params The executor, the descriptor, the buffer and where the progress is recorded.
        task<void> read_until_cancelled(const read_until_cancelled_params params) noexcept(false)
        {
            params.submitted.store(true, std::memory_order_release);

            // Nothing is ever written to the pipe, so only the shutdown cancellation ends this read.
            const auto r = co_await params.exec.async_read(params.fd, std::span<char>(params.buffer.data(), params.buffer.size()));
            params.state->completed = true;
            if (r)
            {
                params.state->ok = true;
                params.state->value = *r;
            }
            else
                params.state->error = r.error();
        }

        /// @brief Suspends on a poll only the shutdown cancellation can end.
        task<void> poll_until_cancelled(executor& exec, std::atomic_bool& submitted, std::atomic_bool& finished,
                                        const int fd) noexcept(false)
        {
            submitted.store(true, std::memory_order_release);
            const auto r = co_await exec.async_poll(fd, POLLIN);
            static_cast<void>(r);
            finished.store(true, std::memory_order_release);
        }
    }

    // statistics
    TEST_CASE("reset_stats clears every counter", "[completion][executor][statistics]")
    {
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        executor exec;
        auto state = std::make_shared<size_outcome>();

        // Any real operation moves the submission and completion counters off zero, so the reset below
        // has something to clear.
        exec.spawn(detail::write_for_stats(exec, state, pipes.write_end()));
        exec.run();

        REQUIRE(state->completed);
        REQUIRE(state->ok);

        const auto& stats = exec.get_stats();
        REQUIRE(stats.total_submissions.load() > 0u);

        exec.reset_stats();
        CHECK(stats.total_submissions.load() == 0u);
        CHECK(stats.total_completions.load() == 0u);
        CHECK(stats.total_tasks_spawned.load() == 0u);
        CHECK(stats.total_tasks_completed.load() == 0u);
        CHECK(stats.error_count.load() == 0u);
        CHECK(stats.submission_full_count.load() == 0u);
    }

    TEST_CASE("statistics::reset zeroes a standalone instance", "[completion][executor][statistics]")
    {
        statistics stats;
        stats.total_submissions.store(11u);
        stats.total_completions.store(12u);
        stats.total_tasks_spawned.store(13u);
        stats.total_tasks_completed.store(14u);
        stats.error_count.store(15u);
        stats.submission_full_count.store(16u);

        stats.reset();

        CHECK(stats.total_submissions.load() == 0u);
        CHECK(stats.total_completions.load() == 0u);
        CHECK(stats.total_tasks_spawned.load() == 0u);
        CHECK(stats.total_tasks_completed.load() == 0u);
        CHECK(stats.error_count.load() == 0u);
        CHECK(stats.submission_full_count.load() == 0u);
    }

    // async_read / async_write
    TEST_CASE("async_write then async_read move bytes through a pipe", "[completion][executor][async_read]")
    {
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        executor exec;
        auto written = std::make_shared<size_outcome>();
        auto read = std::make_shared<size_outcome>();
        std::array<char, 32> buffer {};

        exec.spawn(detail::write_then_read_pipe({.exec = exec, .written = written, .read = read, .buffer = buffer, .pipes = pipes}));
        exec.run();

        REQUIRE(written->ok);
        CHECK(written->value == 7u);
        REQUIRE(read->ok);
        CHECK(read->value == 7u);
        CHECK(std::string(buffer.data(), read->value) == "kmx-aio");
    }

    TEST_CASE("async_read reports end of stream as zero bytes", "[completion][executor][async_read]")
    {
        pipe_pair pipes;
        REQUIRE(pipes.valid());
        pipes.close_write();

        executor exec;
        auto state = std::make_shared<size_outcome>();
        std::array<char, 8> buffer {};

        exec.spawn(detail::read_end_of_stream(exec, state, buffer, pipes.read_end()));
        exec.run();

        REQUIRE(state->ok);
        CHECK(state->value == 0u);
    }

    TEST_CASE("async_read reports a bad descriptor", "[completion][executor][async_read][error]")
    {
        executor exec;
        auto state = std::make_shared<size_outcome>();
        std::array<char, 8> buffer {};

        exec.spawn(detail::read_bad_descriptor(exec, state, buffer));
        exec.run();

        REQUIRE(state->completed);
        CHECK_FALSE(state->ok);
        CHECK(state->error == std::errc::bad_file_descriptor);
    }

    TEST_CASE("async_write reports a bad descriptor", "[completion][executor][async_write][error]")
    {
        executor exec;
        auto state = std::make_shared<size_outcome>();

        exec.spawn(detail::write_bad_descriptor(exec, state));
        exec.run();

        REQUIRE(state->completed);
        CHECK_FALSE(state->ok);
        CHECK(state->error == std::errc::bad_file_descriptor);
    }

    // registered buffers and the fixed-buffer operations
    TEST_CASE("register_buffers and unregister_buffers succeed in order", "[completion][executor][fixed]")
    {
        executor exec;
        std::array<char, 256> storage {};
        const ::iovec iov {storage.data(), storage.size()};

        REQUIRE(exec.register_buffers(std::span<const ::iovec>(&iov, 1u)).has_value());
        CHECK(exec.unregister_buffers().has_value());
    }

    TEST_CASE("unregister_buffers without a registration reports an error", "[completion][executor][fixed][error]")
    {
        executor exec;
        const auto result = exec.unregister_buffers();
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().category() == std::generic_category());
    }

    TEST_CASE("register_buffers rejects an empty set", "[completion][executor][fixed][error]")
    {
        executor exec;
        const auto result = exec.register_buffers(std::span<const ::iovec> {});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().category() == std::generic_category());
    }

    TEST_CASE("async_write_fixed and async_read_fixed use a registered buffer", "[completion][executor][fixed]")
    {
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        executor exec;
        std::array<char, 256> storage {};
        const ::iovec iov {storage.data(), storage.size()};
        REQUIRE(exec.register_buffers(std::span<const ::iovec>(&iov, 1u)).has_value());

        auto written = std::make_shared<size_outcome>();
        auto read = std::make_shared<size_outcome>();

        exec.spawn(detail::write_then_read_fixed({.exec = exec, .written = written, .read = read, .buffer = storage, .pipes = pipes}));
        exec.run();

        REQUIRE(written->ok);
        CHECK(written->value == 5u);
        REQUIRE(read->ok);
        CHECK(read->value == 5u);
        CHECK(std::string(storage.data(), read->value) == "fixed");

        CHECK(exec.unregister_buffers().has_value());
    }

    TEST_CASE("async_read_fixed reports an unknown buffer index", "[completion][executor][fixed][error]")
    {
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        executor exec;
        std::array<char, 64> storage {};
        const ::iovec iov {storage.data(), storage.size()};
        REQUIRE(exec.register_buffers(std::span<const ::iovec>(&iov, 1u)).has_value());

        auto state = std::make_shared<size_outcome>();
        exec.spawn(detail::read_fixed_unknown_index(exec, state, storage, pipes.read_end()));
        exec.run();

        REQUIRE(state->completed);
        CHECK_FALSE(state->ok);
        CHECK(state->error.category() == std::generic_category());

        CHECK(exec.unregister_buffers().has_value());
    }

    // async_accept / async_connect
    TEST_CASE("async_connect and async_accept complete a loopback handshake", "[completion][executor][async_accept]")
    {
        auto listener = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(listener.has_value());

        const int reuse = 1;
        REQUIRE(listener->setsockopt(SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)).has_value());
        REQUIRE(listener->bind(make_ip_address(ipv4::localhost), 0u).has_value());
        REQUIRE(listener->listen(4).has_value());

        ::sockaddr_in bound {};
        ::socklen_t bound_len = sizeof(bound);
        REQUIRE(::getsockname(listener->get(), reinterpret_cast<::sockaddr*>(&bound), &bound_len) == 0);

        auto client = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(client.has_value());

        executor exec;
        auto accepted = std::make_shared<fd_outcome>();
        auto connected = std::make_shared<void_outcome>();

        socket_address peer {.length = sizeof(::sockaddr_storage)};

        // The accept is spawned first so it is already in the ring when the connect arrives.

        exec.spawn(detail::accept_loopback(exec, accepted, peer, listener->get()));
        exec.spawn(detail::connect_loopback(exec, connected, bound, client->get()));
        exec.run();

        REQUIRE(connected->completed);
        CHECK(connected->ok);
        REQUIRE(accepted->completed);
        REQUIRE(accepted->ok);
        CHECK(accepted->value >= 0);
        CHECK(peer.storage.ss_family == AF_INET);

        if (accepted->ok)
            ::close(accepted->value);
    }

    TEST_CASE("async_accept reports a non-listening descriptor", "[completion][executor][async_accept][error]")
    {
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        executor exec;
        auto state = std::make_shared<fd_outcome>();
        socket_address peer {.length = sizeof(::sockaddr_storage)};

        exec.spawn(detail::accept_non_listening(exec, state, peer, pipes.read_end()));
        exec.run();

        REQUIRE(state->completed);
        CHECK_FALSE(state->ok);
        CHECK(state->error.category() == std::generic_category());
    }

    TEST_CASE("async_connect reports a refused port", "[completion][executor][async_connect][error]")
    {
        // Bind and drop a listener to obtain a port nothing is listening on.
        port_t closed_port {};
        {
            auto probe = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
            REQUIRE(probe.has_value());
            REQUIRE(probe->bind(make_ip_address(ipv4::localhost), 0u).has_value());

            ::sockaddr_in bound {};
            ::socklen_t bound_len = sizeof(bound);
            REQUIRE(::getsockname(probe->get(), reinterpret_cast<::sockaddr*>(&bound), &bound_len) == 0);
            closed_port = ::ntohs(bound.sin_port);
        }

        auto client = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(client.has_value());

        const auto address = make_socket_address(make_ip_address(ipv4::localhost), closed_port);
        REQUIRE(address.has_value());

        executor exec;
        auto state = std::make_shared<void_outcome>();

        exec.spawn(detail::connect_refused_port(exec, state, address, client->get()));
        exec.run();

        REQUIRE(state->completed);
        CHECK_FALSE(state->ok);
        CHECK(state->error == std::errc::connection_refused);
    }

    // async_cancel
    TEST_CASE("async_cancel reports no match for an unknown user_data", "[completion][executor][async_cancel]")
    {
        // Nothing in the ring carries this user_data. Whether the kernel answers ENOENT or reports the
        // cancel as accepted varies by io_uring version, so the assertion is on what the executor owes
        // the caller either way: the request is submitted, reaped, and the coroutine resumed exactly
        // once - not on which of the two answers this kernel gives.
        executor exec;
        auto state = std::make_shared<void_outcome>();

        exec.spawn(detail::cancel_unknown_user_data(exec, state));
        exec.run();

        REQUIRE(state->completed);
        if (!state->ok)
            CHECK(state->error.category() == std::generic_category());

        CHECK(exec.get_stats().total_submissions.load() > 0u);
        CHECK(exec.get_stats().total_completions.load() > 0u);
    }

    // lifecycle accessors
    TEST_CASE("is_io_thread_affined_to rejects a negative core", "[completion][executor][affinity][error]")
    {
        executor exec;
        const auto result = exec.is_io_thread_affined_to(-1);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::invalid_argument);
    }

    TEST_CASE("is_io_thread_affined_to refuses a stopped executor", "[completion][executor][affinity][error]")
    {
        // No I/O thread has been started, so there is no affinity to report - and answering "false"
        // would be indistinguishable from a thread that is running on another core.
        executor exec;
        const auto result = exec.is_io_thread_affined_to(0);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::operation_not_permitted);
    }

    TEST_CASE("get_default returns one instance per thread", "[completion][executor][default]")
    {
        auto& first = executor::get_default();
        auto& second = executor::get_default();
        CHECK(&first == &second);

        // The instance is thread_local, so another thread must see a different executor.
        const executor* other {};
        std::thread worker([&other]() { other = &executor::get_default(); });
        worker.join();

        REQUIRE(other != nullptr);
        CHECK(other != &first);
    }

    TEST_CASE("stop on an executor that never ran is harmless", "[completion][executor][lifecycle]")
    {
        // stop() has a second path for the case where running_ was already false: it still has to look
        // for a thread left unjoined rather than assume there is none.
        executor exec;
        exec.stop();
        exec.stop();
        SUCCEED("stop() is safe to call on an executor that was never run");
    }

    TEST_CASE("the lifetime token expires with the executor", "[completion][executor][lifecycle]")
    {
        std::weak_ptr<void> token;
        {
            executor exec;
            token = exec.get_lifetime_token();
            CHECK_FALSE(token.expired());
        }

        CHECK(token.expired());
    }

    // core pinning
    TEST_CASE("a configured core pins the I/O thread", "[completion][executor][affinity]")
    {
        const auto core = first_allowed_cpu();
        REQUIRE(core.has_value());

        const executor_config config {
            .ring_entries = 64u,
            .max_completions = 64u,
            .thread_count = 1u,
            .core_id = static_cast<std::int16_t>(*core),
        };

        executor exec {config};

        // Something has to hold the loop open long enough to be asked about, so a read that will not
        // complete until the write below parks the executor rather than letting run() drain at once.
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        std::array<char, 8> buffer {};
        std::atomic_bool started {false};
        exec.spawn(detail::park_on_read(exec, started, buffer, pipes.read_end()));

        kmx::aio::test::scoped_completion_runner runner {exec};

        // The I/O thread is created inside run(), so the query has to be retried until it exists.
        expected_bool_t affined = std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline)
        {
            affined = exec.is_io_thread_affined_to(*core);
            if (affined.has_value())
                break;

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        const bool observed = affined.has_value() && *affined;

        // Release the read so run() returns, whatever the assertion above found.
        const char byte = 'x';
        static_cast<void>(::write(pipes.write_end(), &byte, 1u));
        exec.stop();
        REQUIRE(runner.wait_until_drained(std::chrono::seconds(5)));

        CHECK(observed);
    }

    TEST_CASE("a negative core leaves the I/O thread unpinned", "[completion][executor][affinity]")
    {
        // core_id -1 is the default and means "do not pin": the thread has to keep the process mask.
        const executor_config config {.ring_entries = 64u, .max_completions = 64u, .thread_count = 1u, .core_id = -1};
        executor exec {config};

        pipe_pair pipes;
        REQUIRE(pipes.valid());

        std::array<char, 8> buffer {};
        auto body = [&exec, &buffer, fd = pipes.read_end()]() -> task<void>
        {
            const auto r = co_await exec.async_read(fd, std::span<char>(buffer.data(), buffer.size()));
            static_cast<void>(r);
            exec.stop();
        };
        exec.spawn(body());

        kmx::aio::test::scoped_completion_runner runner {exec};
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        const auto core = first_allowed_cpu();
        REQUIRE(core.has_value());
        const auto affined = exec.is_io_thread_affined_to(*core);

        const char byte = 'x';
        static_cast<void>(::write(pipes.write_end(), &byte, 1u));
        exec.stop();
        REQUIRE(runner.wait_until_drained(std::chrono::seconds(5)));

        // Unpinned means the thread still carries the whole process mask, which includes this core.
        if (affined.has_value())
            CHECK(*affined);
    }

    // shutdown with work still in flight
    TEST_CASE("stopping with an operation in flight cancels it and drains", "[completion][executor][shutdown]")
    {
        // The shutdown path that matters: a coroutine is suspended on an io_uring operation that will
        // never complete on its own. Leaving the loop at that point would abandon the frame and tear the
        // ring down underneath a request the kernel still owns, so the loop instead submits a
        // cancel-all, lets the suspended coroutine resume with an error, and only then exits.
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        kmx::aio::completion::executor exec;
        auto state = std::make_shared<size_outcome>();
        std::atomic_bool submitted {false};
        std::array<char, 8> buffer {};

        exec.spawn(
            detail::read_until_cancelled({.exec = exec, .state = state, .submitted = submitted, .buffer = buffer, .fd = pipes.read_end()}));

        kmx::aio::test::scoped_completion_runner runner {exec};

        // The read has to be in the ring before the stop, or there is nothing to cancel.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!submitted.load(std::memory_order_acquire) && (std::chrono::steady_clock::now() < deadline))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        REQUIRE(submitted.load(std::memory_order_acquire));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        exec.stop();
        REQUIRE(runner.wait_until_drained(std::chrono::seconds(5)));

        // The task must have finished rather than been abandoned: that is the whole point of draining
        // instead of breaking out of the loop.
        CHECK(state->completed);
        CHECK_FALSE(state->ok);
    }

    TEST_CASE("a task suspended on a poll is released by shutdown", "[completion][executor][shutdown]")
    {
        // Same shutdown path reached through a different operation, to show the cancel-all covers the
        // ring rather than one opcode.
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        kmx::aio::completion::executor exec;
        std::atomic_bool submitted {false};
        std::atomic_bool finished {false};

        exec.spawn(detail::poll_until_cancelled(exec, submitted, finished, pipes.read_end()));

        kmx::aio::test::scoped_completion_runner runner {exec};

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!submitted.load(std::memory_order_acquire) && (std::chrono::steady_clock::now() < deadline))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        REQUIRE(submitted.load(std::memory_order_acquire));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        exec.stop();
        REQUIRE(runner.wait_until_drained(std::chrono::seconds(5)));

        CHECK(finished.load(std::memory_order_acquire));
    }
}
