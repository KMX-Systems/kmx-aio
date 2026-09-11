/// @file src/kmx/aio/modbus/integration/client_server_test.cpp
/// @brief Readiness-model Modbus TCP client against the in-tree server on loopback: registers, coils and exceptions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#if defined(KMX_AIO_FEATURE_MODBUS)
    #ifndef PCH
        #include <kmx/aio/modbus/client.hpp>
        #include <kmx/aio/modbus/error.hpp>
        #include <kmx/aio/modbus/server.hpp>
        #include <kmx/aio/readiness/executor.hpp>
        #include <kmx/aio/task.hpp>

        #include <catch2/catch_test_macros.hpp>

        #include <array>
        #include <cstdint>
        #include <expected>
        #include <memory>
        #include <optional>
        #include <system_error>
        #include <vector>
    #endif

namespace kmx::aio::test::modbus::integration::client_server_test
{
    using namespace kmx::aio::modbus;

    using namespace std::literals::chrono_literals;

    // Constants
    static constexpr std::uint16_t base_port = 15502u;
    static constexpr std::uint8_t unit_id = 0x01u;

    // In-memory register / coil banks for the test server
    struct register_bank
    {
        std::array<std::uint16_t, 65536u> holding {};
        std::array<std::uint16_t, 65536u> input {};
        std::array<std::uint8_t, 65536u> coils {};
        std::array<std::uint8_t, 65536u> discrete {};
    };

    namespace detail
    {
        /// @brief Answers one read-holding-registers request out of the bank.
        /// @param bank The registers and coils the test server serves out of.
        /// @param req The request PDU as the server framed it.
        /// @return The response PDU.
        /// @throws std::bad_alloc (coroutine frame and response allocation).
        [[nodiscard]] task<std::vector<std::uint8_t>> read_holding_registers_response(register_bank& bank,
                                                                                      server_request req) noexcept(false)
        {
            // PDU: fc(1) + addr(2) + count(2)
            if (req.pdu.size() < 5u)
                co_return std::vector<std::uint8_t> {
                    static_cast<std::uint8_t>(static_cast<std::uint8_t>(function_code::read_holding_registers) | 0x80u),
                    static_cast<std::uint8_t>(exception_code::illegal_data_value)};

            const std::uint16_t address = static_cast<std::uint16_t>((static_cast<std::uint16_t>(req.pdu[1]) << 8u) | req.pdu[2]);
            const std::uint16_t count = static_cast<std::uint16_t>((static_cast<std::uint16_t>(req.pdu[3]) << 8u) | req.pdu[4]);

            if ((count == 0u) || (count > 125u) || static_cast<std::size_t>(address) + count > 65536u)
                co_return std::vector<std::uint8_t> {
                    static_cast<std::uint8_t>(static_cast<std::uint8_t>(function_code::read_holding_registers) | 0x80u),
                    static_cast<std::uint8_t>(exception_code::illegal_data_address)};

            const auto byte_count = static_cast<std::uint8_t>(count * 2u);
            std::vector<std::uint8_t> pdu;
            pdu.reserve(2u + byte_count);
            pdu.push_back(static_cast<std::uint8_t>(function_code::read_holding_registers));
            pdu.push_back(byte_count);
            for (std::uint16_t i = 0u; i < count; ++i)
            {
                const std::uint16_t v = bank.holding[address + i];
                pdu.push_back(static_cast<std::uint8_t>(v >> 8u));
                pdu.push_back(static_cast<std::uint8_t>(v & 0xFFu));
            }

            co_return pdu;
        }

        /// @brief Writes one block of holding registers into the bank and echoes the request back.
        /// @param bank The registers and coils the test server serves out of.
        /// @param req The request PDU as the server framed it.
        /// @return The response PDU.
        /// @throws std::bad_alloc (coroutine frame and response allocation).
        [[nodiscard]] task<std::vector<std::uint8_t>> write_multiple_registers_response(register_bank& bank,
                                                                                        server_request req) noexcept(false)
        {
            // PDU: fc(1) + addr(2) + count(2) + byte_count(1) + data(...)
            if (req.pdu.size() < 6u)
                co_return std::vector<std::uint8_t> {
                    static_cast<std::uint8_t>(static_cast<std::uint8_t>(function_code::write_multiple_registers) | 0x80u),
                    static_cast<std::uint8_t>(exception_code::illegal_data_value)};

            const std::uint16_t address = static_cast<std::uint16_t>((static_cast<std::uint16_t>(req.pdu[1]) << 8u) | req.pdu[2]);
            const std::uint16_t count = static_cast<std::uint16_t>((static_cast<std::uint16_t>(req.pdu[3]) << 8u) | req.pdu[4]);

            for (std::uint16_t i = 0u; i < count; ++i)
            {
                const std::size_t offset = 6u + static_cast<std::size_t>(i) * 2u;
                if (offset + 1u >= req.pdu.size())
                    break;
                const std::uint16_t v =
                    static_cast<std::uint16_t>((static_cast<std::uint16_t>(req.pdu[offset]) << 8u) | req.pdu[offset + 1u]);
                bank.holding[address + i] = v;
            }

            // Echo: fc + addr + count
            std::vector<std::uint8_t> pdu(5u);
            pdu[0] = static_cast<std::uint8_t>(function_code::write_multiple_registers);
            pdu[1] = static_cast<std::uint8_t>(address >> 8u);
            pdu[2] = static_cast<std::uint8_t>(address & 0xFFu);
            pdu[3] = static_cast<std::uint8_t>(count >> 8u);
            pdu[4] = static_cast<std::uint8_t>(count & 0xFFu);
            co_return pdu;
        }

        /// @brief Answers one read-coils request out of the bank.
        /// @param bank The registers and coils the test server serves out of.
        /// @param req The request PDU as the server framed it.
        /// @return The response PDU.
        /// @throws std::bad_alloc (coroutine frame and response allocation).
        [[nodiscard]] task<std::vector<std::uint8_t>> read_coils_response(register_bank& bank, server_request req) noexcept(false)
        {
            if (req.pdu.size() < 5u)
                co_return std::vector<std::uint8_t> {
                    static_cast<std::uint8_t>(static_cast<std::uint8_t>(function_code::read_coils) | 0x80u),
                    static_cast<std::uint8_t>(exception_code::illegal_data_value)};

            const std::uint16_t address = static_cast<std::uint16_t>((static_cast<std::uint16_t>(req.pdu[1]) << 8u) | req.pdu[2]);
            const std::uint16_t count = static_cast<std::uint16_t>((static_cast<std::uint16_t>(req.pdu[3]) << 8u) | req.pdu[4]);

            const auto byte_count = static_cast<std::uint8_t>((count + 7u) / 8u);
            std::vector<std::uint8_t> pdu(2u + byte_count, 0u);
            pdu[0] = static_cast<std::uint8_t>(function_code::read_coils);
            pdu[1] = byte_count;
            for (std::uint16_t i = 0u; i < count; ++i)
                if (bank.coils[address + i] != 0u)
                    pdu[2u + i / 8u] |= static_cast<std::uint8_t>(1u << (i % 8u));
            co_return pdu;
        }

        /// @brief Writes one coil into the bank and echoes the request PDU back.
        /// @param bank The registers and coils the test server serves out of.
        /// @param req The request PDU as the server framed it.
        /// @return The response PDU.
        /// @throws std::bad_alloc (coroutine frame and response allocation).
        [[nodiscard]] task<std::vector<std::uint8_t>> write_single_coil_response(register_bank& bank, server_request req) noexcept(false)
        {
            if (req.pdu.size() < 5u)
                co_return std::vector<std::uint8_t> {
                    static_cast<std::uint8_t>(static_cast<std::uint8_t>(function_code::write_single_coil) | 0x80u),
                    static_cast<std::uint8_t>(exception_code::illegal_data_value)};

            const std::uint16_t address = static_cast<std::uint16_t>((static_cast<std::uint16_t>(req.pdu[1]) << 8u) | req.pdu[2]);
            const bool on = (req.pdu[3] == 0xFFu);
            bank.coils[address] = on ? 1u : 0u;

            // Echo back the request PDU (spec §6.5)
            co_return req.pdu;
        }
    }

    // Server handler factories
    [[nodiscard]] static request_handler make_read_holding_handler(register_bank& bank)
    {
        return [&bank](server_request req) { return detail::read_holding_registers_response(bank, std::move(req)); };
    }

    [[nodiscard]] static request_handler make_write_multiple_registers_handler(register_bank& bank)
    {
        return [&bank](server_request req) { return detail::write_multiple_registers_response(bank, std::move(req)); };
    }

    [[nodiscard]] static request_handler make_read_coils_handler(register_bank& bank)
    {
        return [&bank](server_request req) { return detail::read_coils_response(bank, std::move(req)); };
    }

    [[nodiscard]] static request_handler make_write_single_coil_handler(register_bank& bank)
    {
        return [&bank](server_request req) { return detail::write_single_coil_response(bank, std::move(req)); };
    }

    // Test fixture helpers
    /// @brief What the connect exchange observed, read by the test once the executor has drained.
    struct connection_outcome
    {
        /// @brief Set once the connect attempt finished.
        bool completed {};
        /// @brief Why the connect failed, if it did.
        std::optional<std::error_code> error;
    };

    /// @brief What the holding-register exchange observed, read by the test once the executor has drained.
    struct registers_outcome
    {
        /// @brief Set once the exchange ran to its end.
        bool completed {};
        /// @brief The registers as last read: after the write when it succeeded, before it otherwise.
        std::optional<register_values> read_result {};
        /// @brief The error of the last operation that failed, if any did.
        std::optional<std::error_code> op_error {};
    };

    /// @brief What the coil exchange observed, read by the test once the executor has drained.
    struct coils_outcome
    {
        /// @brief Set once the exchange ran to its end.
        bool completed {};
        /// @brief The coils as read before the write.
        std::optional<coil_values> initial_coils {};
        /// @brief The coils as read after the write.
        std::optional<coil_values> post_write_coils {};
        /// @brief The error of the last operation that failed, if any did.
        std::optional<std::error_code> op_error {};
    };

    namespace detail
    {
        /// @brief Waits for the listener, connects, records the outcome, and stops the server.
        task<void> connect_and_disconnect(connection_outcome& state, const std::shared_ptr<readiness::executor>& exec,
                                          const std::shared_ptr<server>& srv) noexcept(false)
        {
            static_cast<void>(co_await exec->async_timeout(5'000'000u)); // 5 ms
            client c {{.host = "127.0.0.1", .port = base_port, .unit_id = unit_id}, *exec};
            const auto r = co_await c.connect();
            state.error = r ? std::optional<std::error_code> {} : std::optional {r.error()};
            state.completed = true;
            static_cast<void>(co_await c.disconnect());
            srv->stop();
        }

        /// @brief Reads the holding registers, writes new values, and reads them back.
        /// @param outcome Receives what the exchange observed.
        task<void> exchange_holding_registers(registers_outcome& outcome, const std::shared_ptr<readiness::executor>& exec,
                                              const std::shared_ptr<server>& srv) noexcept(false)
        {
            static_cast<void>(co_await exec->async_timeout(5'000'000u));
            client c {{.host = "127.0.0.1", .port = base_port + 1u, .unit_id = unit_id}, *exec};

            if (const auto r = co_await c.connect(); !r)
            {
                outcome.op_error = r.error();
                srv->stop();
                co_return;
            }

            // Read initial values
            const auto read1 = co_await c.read_holding_registers(100u, 3u);
            if (!read1)
                outcome.op_error = read1.error();
            else
                outcome.read_result = *read1;

            // Write new values then read back
            const std::vector<std::uint16_t> new_vals {7u, 8u, 9u};
            const auto write_r = co_await c.write_multiple_registers(100u, new_vals);
            if (!write_r)
                outcome.op_error = write_r.error();
            else
            {
                const auto read2 = co_await c.read_holding_registers(100u, 3u);
                if (!read2)
                    outcome.op_error = read2.error();
                else
                    outcome.read_result = *read2; // overwrite with post-write read
            }

            outcome.completed = true;
            static_cast<void>(co_await c.disconnect());
            srv->stop();
        }

        /// @brief Reads the coils, flips one off, and reads them back.
        /// @param outcome Receives what the exchange observed.
        task<void> exchange_coils(coils_outcome& outcome, const std::shared_ptr<readiness::executor>& exec,
                                  const std::shared_ptr<server>& srv) noexcept(false)
        {
            static_cast<void>(co_await exec->async_timeout(5'000'000u));
            client c {{.host = "127.0.0.1", .port = base_port + 2u, .unit_id = unit_id}, *exec};

            if (const auto r = co_await c.connect(); !r)
            {
                outcome.op_error = r.error();
                srv->stop();
                co_return;
            }

            if (const auto r = co_await c.read_coils(0u, 3u); r)
                outcome.initial_coils = *r;
            else
                outcome.op_error = r.error();

            // Flip coil 0 OFF
            if (const auto r = co_await c.write_single_coil(0u, false); !r)
                outcome.op_error = r.error();

            if (const auto r = co_await c.read_coils(0u, 3u); r)
                outcome.post_write_coils = *r;
            else
                outcome.op_error = r.error();

            outcome.completed = true;
            static_cast<void>(co_await c.disconnect());
            srv->stop();
        }

        /// @brief Issues a request for a function code the server has no handler for.
        task<void> exchange_unregistered(bool& completed, std::optional<std::error_code>& result_error,
                                         const std::shared_ptr<readiness::executor>& exec,
                                         const std::shared_ptr<server>& srv) noexcept(false)
        {
            static_cast<void>(co_await exec->async_timeout(5'000'000u));
            client c {{.host = "127.0.0.1", .port = base_port + 3u, .unit_id = unit_id}, *exec};

            if (const auto r = co_await c.connect(); !r)
            {
                result_error = r.error();
                srv->stop();
                co_return;
            }

            const auto r = co_await c.read_holding_registers(0u, 1u);
            if (!r)
                result_error = r.error();

            completed = true;
            static_cast<void>(co_await c.disconnect());
            srv->stop();
        }
    }

    // Integration Tests
    TEST_CASE("modbus integration: client connects and disconnects", "[modbus][integration][slow]")
    {
        register_bank bank {};
        auto srv = std::make_shared<server>();
        srv->set_handler(function_code::read_holding_registers, make_read_holding_handler(bank));

        auto exec = std::make_shared<readiness::executor>();
        connection_outcome state {};
        const server_config config {.bind_address = "127.0.0.1", .port = base_port, .unit_id = unit_id};

        auto serve = [exec, srv, config]() -> task<void> { static_cast<void>(co_await srv->serve(*exec, config)); };
        exec->spawn(serve());

        exec->spawn(detail::connect_and_disconnect(state, exec, srv));

        exec->run();

        REQUIRE(state.completed);
        REQUIRE(!state.error.has_value());
    }

    TEST_CASE("modbus integration: read and write holding registers", "[modbus][integration][slow]")
    {
        register_bank bank {};
        bank.holding[100] = 42u;
        bank.holding[101] = 1000u;
        bank.holding[102] = 65535u;

        auto srv = std::make_shared<server>();
        srv->set_handler(function_code::read_holding_registers, make_read_holding_handler(bank));
        srv->set_handler(function_code::write_multiple_registers, make_write_multiple_registers_handler(bank));

        auto exec = std::make_shared<readiness::executor>();

        registers_outcome outcome {};

        auto serve = [exec, srv]() -> task<void>
        { static_cast<void>(co_await srv->serve(*exec, {.bind_address = "127.0.0.1", .port = base_port + 1u, .unit_id = unit_id})); };
        exec->spawn(serve());

        exec->spawn(detail::exchange_holding_registers(outcome, exec, srv));

        exec->run();

        REQUIRE(outcome.completed);
        REQUIRE(!outcome.op_error.has_value());
        REQUIRE(outcome.read_result.has_value());
        REQUIRE(outcome.read_result->size() == 3u);
        CHECK(outcome.read_result->at(0) == 7u);
        CHECK(outcome.read_result->at(1) == 8u);
        CHECK(outcome.read_result->at(2) == 9u);
    }

    TEST_CASE("modbus integration: read and write coils", "[modbus][integration][slow]")
    {
        register_bank bank {};
        bank.coils[0] = 1u;
        bank.coils[1] = 0u;
        bank.coils[2] = 1u;

        auto srv = std::make_shared<server>();
        srv->set_handler(function_code::read_coils, make_read_coils_handler(bank));
        srv->set_handler(function_code::write_single_coil, make_write_single_coil_handler(bank));

        auto exec = std::make_shared<readiness::executor>();

        coils_outcome outcome {};

        auto serve = [exec, srv]() -> task<void>
        { static_cast<void>(co_await srv->serve(*exec, {.bind_address = "127.0.0.1", .port = base_port + 2u, .unit_id = unit_id})); };
        exec->spawn(serve());

        exec->spawn(detail::exchange_coils(outcome, exec, srv));

        exec->run();

        REQUIRE(outcome.completed);
        REQUIRE(!outcome.op_error.has_value());
        REQUIRE(outcome.initial_coils.has_value());
        REQUIRE(outcome.initial_coils->size() == 3u);
        CHECK(outcome.initial_coils->at(0) == 1u);
        CHECK(outcome.initial_coils->at(1) == 0u);
        CHECK(outcome.initial_coils->at(2) == 1u);

        REQUIRE(outcome.post_write_coils.has_value());
        CHECK(outcome.post_write_coils->at(0) == 0u); // flipped OFF
        CHECK(outcome.post_write_coils->at(1) == 0u);
        CHECK(outcome.post_write_coils->at(2) == 1u);
    }

    TEST_CASE("modbus integration: unregistered function code returns exception", "[modbus][integration][slow]")
    {
        auto srv = std::make_shared<server>();
        // No handlers registered — all requests should return illegal_function

        auto exec = std::make_shared<readiness::executor>();

        bool completed {};
        std::optional<std::error_code> result_error;

        auto serve = [exec, srv]() -> task<void>
        { static_cast<void>(co_await srv->serve(*exec, {.bind_address = "127.0.0.1", .port = base_port + 3u, .unit_id = unit_id})); };
        exec->spawn(serve());

        exec->spawn(detail::exchange_unregistered(completed, result_error, exec, srv));

        exec->run();

        REQUIRE(completed);
        REQUIRE(result_error.has_value());
        CHECK(*result_error == make_error_code(error::exception_response));
    }

}
#endif // KMX_AIO_FEATURE_MODBUS
