/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#if defined(KMX_AIO_FEATURE_MODBUS)
    #include <kmx/aio/modbus/client.hpp>
    #include <kmx/aio/modbus/server.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/task.hpp>

    #include <array>
    #include <atomic>
    #include <cstdint>
    #include <expected>
    #include <memory>
    #include <optional>
    #include <string>
    #include <system_error>
    #include <utility>
    #include <vector>

namespace kmx::aio::modbus::test::integration
{
    using namespace std::literals::chrono_literals;

    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr std::uint16_t test_port    = 15502u;
    static constexpr std::uint8_t  test_unit_id = 0x01u;

    // =========================================================================
    // In-memory register / coil banks for the test server
    // =========================================================================

    struct register_bank
    {
        std::array<std::uint16_t, 65536u> holding {};
        std::array<std::uint16_t, 65536u> input   {};
        std::array<std::uint8_t,  65536u> coils   {};
        std::array<std::uint8_t,  65536u> discrete{};
    };

    // =========================================================================
    // Server handler factories
    // =========================================================================

    [[nodiscard]] static request_handler make_read_holding_handler(register_bank& bank)
    {
        return [&bank](server_request req) -> task<std::vector<std::uint8_t>>
        {
            // PDU: fc(1) + addr(2) + count(2)
            if (req.pdu.size() < 5u)
                co_return std::vector<std::uint8_t> {
                    static_cast<std::uint8_t>(static_cast<std::uint8_t>(function_code::read_holding_registers) | 0x80u),
                    static_cast<std::uint8_t>(exception_code::illegal_data_value)};

            const std::uint16_t address = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(req.pdu[1]) << 8u) | req.pdu[2]);
            const std::uint16_t count = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(req.pdu[3]) << 8u) | req.pdu[4]);

            if (count == 0u || count > 125u || static_cast<std::size_t>(address) + count > 65536u)
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
        };
    }

    [[nodiscard]] static request_handler make_write_multiple_registers_handler(register_bank& bank)
    {
        return [&bank](server_request req) -> task<std::vector<std::uint8_t>>
        {
            // PDU: fc(1) + addr(2) + count(2) + byte_count(1) + data(...)
            if (req.pdu.size() < 6u)
                co_return std::vector<std::uint8_t> {
                    static_cast<std::uint8_t>(static_cast<std::uint8_t>(function_code::write_multiple_registers) | 0x80u),
                    static_cast<std::uint8_t>(exception_code::illegal_data_value)};

            const std::uint16_t address = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(req.pdu[1]) << 8u) | req.pdu[2]);
            const std::uint16_t count = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(req.pdu[3]) << 8u) | req.pdu[4]);

            for (std::uint16_t i = 0u; i < count; ++i)
            {
                const std::size_t offset = 6u + static_cast<std::size_t>(i) * 2u;
                if (offset + 1u >= req.pdu.size())
                    break;
                const std::uint16_t v = static_cast<std::uint16_t>(
                    (static_cast<std::uint16_t>(req.pdu[offset]) << 8u) | req.pdu[offset + 1u]);
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
        };
    }

    [[nodiscard]] static request_handler make_read_coils_handler(register_bank& bank)
    {
        return [&bank](server_request req) -> task<std::vector<std::uint8_t>>
        {
            if (req.pdu.size() < 5u)
                co_return std::vector<std::uint8_t> {
                    static_cast<std::uint8_t>(static_cast<std::uint8_t>(function_code::read_coils) | 0x80u),
                    static_cast<std::uint8_t>(exception_code::illegal_data_value)};

            const std::uint16_t address = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(req.pdu[1]) << 8u) | req.pdu[2]);
            const std::uint16_t count = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(req.pdu[3]) << 8u) | req.pdu[4]);

            const auto byte_count = static_cast<std::uint8_t>((count + 7u) / 8u);
            std::vector<std::uint8_t> pdu(2u + byte_count, 0u);
            pdu[0] = static_cast<std::uint8_t>(function_code::read_coils);
            pdu[1] = byte_count;
            for (std::uint16_t i = 0u; i < count; ++i)
            {
                if (bank.coils[address + i] != 0u)
                    pdu[2u + i / 8u] |= static_cast<std::uint8_t>(1u << (i % 8u));
            }
            co_return pdu;
        };
    }

    [[nodiscard]] static request_handler make_write_single_coil_handler(register_bank& bank)
    {
        return [&bank](server_request req) -> task<std::vector<std::uint8_t>>
        {
            if (req.pdu.size() < 5u)
                co_return std::vector<std::uint8_t> {
                    static_cast<std::uint8_t>(static_cast<std::uint8_t>(function_code::write_single_coil) | 0x80u),
                    static_cast<std::uint8_t>(exception_code::illegal_data_value)};

            const std::uint16_t address = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(req.pdu[1]) << 8u) | req.pdu[2]);
            const bool on = (req.pdu[3] == 0xFFu);
            bank.coils[address] = on ? 1u : 0u;

            // Echo back the request PDU (spec §6.5)
            co_return req.pdu;
        };
    }

    // =========================================================================
    // Test fixture helpers
    // =========================================================================

    struct test_state
    {
        bool completed = false;
        std::optional<std::error_code> error;
    };

    // =========================================================================
    // Integration Tests
    // =========================================================================

    TEST_CASE("modbus integration: client connects and disconnects", "[modbus][integration][slow]")
    {
        register_bank bank {};
        auto srv = std::make_shared<server>();
        srv->set_handler(function_code::read_holding_registers, make_read_holding_handler(bank));

        auto exec = std::make_shared<readiness::executor>();
        test_state state {};

        exec->spawn(
            [exec, srv, &config = server_config {.bind_address = "127.0.0.1", .port = test_port, .unit_id = test_unit_id}]()
                -> task<void> { co_await srv->serve(*exec, config); }());

        exec->spawn(
            [&state, exec, srv]() -> task<void>
            {
                co_await exec->async_timeout(5'000'000u); // 5 ms
                client c {{.host = "127.0.0.1", .port = test_port, .unit_id = test_unit_id}, *exec};
                const auto r = co_await c.connect();
                state.error = r ? std::optional<std::error_code> {} : std::optional {r.error()};
                state.completed = true;
                co_await c.disconnect();
                srv->stop();
                exec->stop();
            }());

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

        bool completed = false;
        std::optional<register_values> read_result;
        std::optional<std::error_code> op_error;

        exec->spawn(
            [exec, srv]() -> task<void>
            {
                co_await srv->serve(*exec,
                                    {.bind_address = "127.0.0.1", .port = test_port + 1u, .unit_id = test_unit_id});
            }());

        exec->spawn(
            [&, exec, srv]() -> task<void>
            {
                co_await exec->async_timeout(5'000'000u);
                client c {{.host = "127.0.0.1", .port = test_port + 1u, .unit_id = test_unit_id}, *exec};

                if (const auto r = co_await c.connect(); !r)
                {
                    op_error = r.error();
                    srv->stop();
                    exec->stop();
                    co_return;
                }

                // Read initial values
                const auto read1 = co_await c.read_holding_registers(100u, 3u);
                if (!read1)
                {
                    op_error = read1.error();
                }
                else
                {
                    read_result = *read1;
                }

                // Write new values then read back
                const std::vector<std::uint16_t> new_vals {7u, 8u, 9u};
                const auto write_r = co_await c.write_multiple_registers(100u, new_vals);
                if (!write_r)
                {
                    op_error = write_r.error();
                }
                else
                {
                    const auto read2 = co_await c.read_holding_registers(100u, 3u);
                    if (!read2)
                        op_error = read2.error();
                    else
                        read_result = *read2; // overwrite with post-write read
                }

                completed = true;
                co_await c.disconnect();
                srv->stop();
                exec->stop();
            }());

        exec->run();

        REQUIRE(completed);
        REQUIRE(!op_error.has_value());
        REQUIRE(read_result.has_value());
        REQUIRE(read_result->size() == 3u);
        CHECK(read_result->at(0) == 7u);
        CHECK(read_result->at(1) == 8u);
        CHECK(read_result->at(2) == 9u);
    }

    TEST_CASE("modbus integration: read and write coils", "[modbus][integration][slow]")
    {
        register_bank bank {};
        bank.coils[0] = 1u;
        bank.coils[1] = 0u;
        bank.coils[2] = 1u;

        auto srv = std::make_shared<server>();
        srv->set_handler(function_code::read_coils,       make_read_coils_handler(bank));
        srv->set_handler(function_code::write_single_coil, make_write_single_coil_handler(bank));

        auto exec = std::make_shared<readiness::executor>();

        bool completed = false;
        std::optional<coil_values> initial_coils;
        std::optional<coil_values> post_write_coils;
        std::optional<std::error_code> op_error;

        exec->spawn(
            [exec, srv]() -> task<void>
            {
                co_await srv->serve(*exec,
                                    {.bind_address = "127.0.0.1", .port = test_port + 2u, .unit_id = test_unit_id});
            }());

        exec->spawn(
            [&, exec, srv]() -> task<void>
            {
                co_await exec->async_timeout(5'000'000u);
                client c {{.host = "127.0.0.1", .port = test_port + 2u, .unit_id = test_unit_id}, *exec};

                if (const auto r = co_await c.connect(); !r)
                {
                    op_error = r.error();
                    srv->stop();
                    exec->stop();
                    co_return;
                }

                if (const auto r = co_await c.read_coils(0u, 3u); r)
                    initial_coils = *r;
                else
                    op_error = r.error();

                // Flip coil 0 OFF
                if (const auto r = co_await c.write_single_coil(0u, false); !r)
                    op_error = r.error();

                if (const auto r = co_await c.read_coils(0u, 3u); r)
                    post_write_coils = *r;
                else
                    op_error = r.error();

                completed = true;
                co_await c.disconnect();
                srv->stop();
                exec->stop();
            }());

        exec->run();

        REQUIRE(completed);
        REQUIRE(!op_error.has_value());
        REQUIRE(initial_coils.has_value());
        REQUIRE(initial_coils->size() == 3u);
        CHECK(initial_coils->at(0) == 1u);
        CHECK(initial_coils->at(1) == 0u);
        CHECK(initial_coils->at(2) == 1u);

        REQUIRE(post_write_coils.has_value());
        CHECK(post_write_coils->at(0) == 0u); // flipped OFF
        CHECK(post_write_coils->at(1) == 0u);
        CHECK(post_write_coils->at(2) == 1u);
    }

    TEST_CASE("modbus integration: unregistered function code returns exception", "[modbus][integration][slow]")
    {
        auto srv = std::make_shared<server>();
        // No handlers registered — all requests should return illegal_function

        auto exec = std::make_shared<readiness::executor>();

        bool completed = false;
        std::optional<std::error_code> result_error;

        exec->spawn(
            [exec, srv]() -> task<void>
            {
                co_await srv->serve(*exec,
                                    {.bind_address = "127.0.0.1", .port = test_port + 3u, .unit_id = test_unit_id});
            }());

        exec->spawn(
            [&, exec, srv]() -> task<void>
            {
                co_await exec->async_timeout(5'000'000u);
                client c {{.host = "127.0.0.1", .port = test_port + 3u, .unit_id = test_unit_id}, *exec};

                if (const auto r = co_await c.connect(); !r)
                {
                    result_error = r.error();
                    srv->stop();
                    exec->stop();
                    co_return;
                }

                const auto r = co_await c.read_holding_registers(0u, 1u);
                if (!r)
                    result_error = r.error();

                completed = true;
                co_await c.disconnect();
                srv->stop();
                exec->stop();
            }());

        exec->run();

        REQUIRE(completed);
        REQUIRE(result_error.has_value());
        CHECK(*result_error == make_error_code(error::exception_response));
    }

} // namespace kmx::aio::modbus::test::integration
#endif // KMX_AIO_FEATURE_MODBUS
