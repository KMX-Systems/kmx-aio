/// @file aio/modbus/integration/server_shutdown_test.cpp
/// @brief Regression tests for: modbus server::stop() must actually stop the server.
///
/// Bug reproduced: stop() only requested its stop token, and the token was read at the top of the
/// accept loop. serve() spends its life suspended inside that accept, so the request was never
/// observed: the task stayed outstanding, and an executor whose run() returns once its work drains
/// never returned. Every other test hid this by calling executor::stop() as well, which tears the loop
/// down regardless of what is still pending.
///
/// A connection had the same problem for the same reason - it parks in a read between requests, and
/// nothing woke it - plus one of its own: handle_connection() ignored what process_request() did, so a
/// peer that disconnected left it looping on a dead socket.
///
/// Tests:
///   1. stop() ends serve() while it is waiting for connections.
///   2. stop() ends a session whose peer is connected but idle.
///   3. A peer that disconnects leaves nothing behind, and its exchange still succeeded.
///
/// Each test asserts that run() returns on its own. No executor::stop() is called anywhere here - that
/// is the whole point, and calling it would restore exactly the blind spot these tests exist to cover.

#include <catch2/catch_test_macros.hpp>

#if defined(KMX_AIO_FEATURE_MODBUS)
    #include <kmx/aio/modbus/client.hpp>
    #include <kmx/aio/modbus/error.hpp>
    #include <kmx/aio/modbus/server.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/task.hpp>
    #include <kmx/aio/test/executor_runner.hpp>

    #include <atomic>
    #include <chrono>
    #include <cstdint>
    #include <memory>
    #include <optional>
    #include <vector>

namespace kmx::aio::modbus::test::integration
{
    using namespace std::literals::chrono_literals;
    using kmx::aio::test::scoped_runner;
    using kmx::aio::test::wait_for_flag;

    static constexpr std::uint16_t shutdown_base_port = 15910u;
    static constexpr std::uint8_t  shutdown_unit_id   = 0x01u;

    /// @brief Answers every read of holding registers with a fixed value, so a test can confirm that a
    ///        request really was served before shutdown is examined.
    [[nodiscard]] static request_handler make_constant_holding_handler()
    {
        return [](server_request req) -> task<std::vector<std::uint8_t>>
        {
            const auto fc = static_cast<std::uint8_t>(function_code::read_holding_registers);
            if (req.pdu.size() < 5u)
                co_return std::vector<std::uint8_t> {static_cast<std::uint8_t>(fc | 0x80u),
                                                     static_cast<std::uint8_t>(exception_code::illegal_data_value)};

            co_return std::vector<std::uint8_t> {fc, 2u, 0x00u, 0x2Au}; // one register, value 42
        };
    }

    [[nodiscard]] static server_config config_for(const std::uint16_t port) noexcept
    {
        return server_config {.bind_address = "127.0.0.1", .port = port, .unit_id = shutdown_unit_id};
    }

    // =========================================================================
    // 1. stop() while the server is waiting for a connection
    // =========================================================================

    TEST_CASE("modbus server: stop ends an idle accept loop", "[modbus][server][shutdown][integration]")
    {
        constexpr std::uint16_t port = shutdown_base_port;

        auto srv  = std::make_shared<server>();
        auto exec = std::make_shared<readiness::executor>();
        srv->set_handler(function_code::read_holding_registers, make_constant_holding_handler());

        std::atomic_bool serving {false};
        std::optional<std::error_code> serve_error;

        auto serve = [exec, srv, &serving, &serve_error, port]() -> task<void>
        {
            serving.store(true, std::memory_order_release);
            if (const auto r = co_await srv->serve(*exec, config_for(port)); !r)
                serve_error = r.error();
        };
        exec->spawn(serve());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(serving, 2s));

        // Long enough for serve() to have reached the accept it will be suspended in.
        std::this_thread::sleep_for(100ms);
        srv->stop();

        REQUIRE(runner.wait_until_drained(5s));

        // Shutting down is not a failure: serve() returns success once it is asked to stop.
        CHECK_FALSE(serve_error.has_value());
    }

    // =========================================================================
    // 2. stop() while a peer is connected but sending nothing
    // =========================================================================

    TEST_CASE("modbus server: stop ends a connected idle session", "[modbus][server][shutdown][integration]")
    {
        constexpr std::uint16_t port = shutdown_base_port + 1u;

        auto srv  = std::make_shared<server>();
        auto exec = std::make_shared<readiness::executor>();
        srv->set_handler(function_code::read_holding_registers, make_constant_holding_handler());

        std::atomic_bool connected {false};
        std::optional<std::error_code> connect_error;

        auto serve = [exec, srv, port]() -> task<void> { co_await srv->serve(*exec, config_for(port)); };
        exec->spawn(serve());

        // The client outlives the coroutine that connects it, so the connection stays open - and the
        // session on the server side stays parked in the read it performs between requests - without a
        // task of its own being left outstanding. A coroutine holding the socket open by sleeping would
        // have to be waited for as well, and would then be measuring its own sleep rather than stop().
        auto peer = std::make_shared<client>(client_config {.host    = "127.0.0.1",
                                                            .port    = port,
                                                            .unit_id = shutdown_unit_id},
                                             *exec);

        auto connect_only = [exec, peer, &connected, &connect_error]() -> task<void>
        {
            co_await exec->async_timeout(20'000'000u); // 20 ms, for the listener to be up

            if (const auto r = co_await peer->connect(); !r)
                connect_error = r.error();

            connected.store(true, std::memory_order_release);
        };
        exec->spawn(connect_only());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(connected, 3s));
        REQUIRE_FALSE(connect_error.has_value());

        std::this_thread::sleep_for(100ms);
        srv->stop();

        REQUIRE(runner.wait_until_drained(5s));
    }

    // =========================================================================
    // 3. a peer that disconnects must not leave its session behind
    // =========================================================================

    TEST_CASE("modbus server: a disconnected peer leaves no session behind", "[modbus][server][shutdown][integration]")
    {
        constexpr std::uint16_t port = shutdown_base_port + 2u;

        auto srv  = std::make_shared<server>();
        auto exec = std::make_shared<readiness::executor>();
        srv->set_handler(function_code::read_holding_registers, make_constant_holding_handler());

        std::atomic_bool exchanged {false};
        std::optional<register_values> values;
        std::optional<std::error_code> op_error;

        auto serve = [exec, srv, port]() -> task<void> { co_await srv->serve(*exec, config_for(port)); };
        exec->spawn(serve());

        auto exchange = [exec, &exchanged, &values, &op_error, port]() -> task<void>
        {
            co_await exec->async_timeout(20'000'000u);
            client c {{.host = "127.0.0.1", .port = port, .unit_id = shutdown_unit_id}, *exec};

            if (const auto r = co_await c.connect(); !r)
            {
                op_error = r.error();
                exchanged.store(true, std::memory_order_release);
                co_return;
            }

            if (const auto r = co_await c.read_holding_registers(0u, 1u); r)
                values = *r;
            else
                op_error = r.error();

            // The server's session task must notice this and finish. Before the fix it could not tell a
            // closed connection from a served request, so it kept reading from a socket that was gone.
            co_await c.disconnect();
            exchanged.store(true, std::memory_order_release);
        };
        exec->spawn(exchange());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(exchanged, 5s));
        REQUIRE_FALSE(op_error.has_value());
        REQUIRE(values.has_value());
        REQUIRE(values->size() == 1u);
        CHECK(values->at(0) == 42u);

        // Only the accept loop should still be outstanding by now; stopping it must drain the executor.
        std::this_thread::sleep_for(100ms);
        srv->stop();

        REQUIRE(runner.wait_until_drained(5s));
    }

} // namespace kmx::aio::modbus::test::integration
#endif // KMX_AIO_FEATURE_MODBUS
