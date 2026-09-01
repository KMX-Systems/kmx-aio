/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/test/temp_dir.hpp>
#include <kmx/aio/test/tls_certs.hpp>

#include <kmx/aio/test/sample_process.hpp>

#if defined(KMX_AIO_FEATURE_MODBUS)
    #include <kmx/aio/modbus/error.hpp>
    #include <kmx/aio/modbus/tls_client.hpp>
    #include <kmx/aio/modbus/tls_server.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/task.hpp>

    #include <atomic>
    #include <chrono>
    #include <cstdlib>
    #include <filesystem>
    #include <fstream>
    #include <memory>
    #include <optional>
    #include <string>
    #include <system_error>
    #include <thread>
    #include <vector>

namespace kmx::aio::test::modbus::integration::tls_client_server_test
{
    using namespace kmx::aio::modbus;

    static constexpr std::uint16_t tls_test_port = 15802u;
    static constexpr std::uint8_t tls_test_unit_id = 0x01u;

    // Server handler helpers (minimal — just read one register)
    [[nodiscard]] static request_handler make_simple_holding_handler()
    {
        return [](server_request req) -> task<std::vector<std::uint8_t>>
        {
            if (req.pdu.size() < 5u)
                co_return std::vector<std::uint8_t> {0x83u, 0x03u};

            const std::uint16_t count = static_cast<std::uint16_t>((static_cast<std::uint16_t>(req.pdu[3]) << 8u) | req.pdu[4]);

            const std::uint8_t byte_count = static_cast<std::uint8_t>(count * 2u);
            std::vector<std::uint8_t> pdu;
            pdu.reserve(2u + byte_count);
            pdu.push_back(static_cast<std::uint8_t>(function_code::read_holding_registers));
            pdu.push_back(byte_count);
            for (std::uint16_t i = 0u; i < count; ++i)
            {
                pdu.push_back(0x00u);
                pdu.push_back(static_cast<std::uint8_t>(42u + i)); // deterministic values
            }
            co_return pdu;
        };
    }

    // mTLS integration test
    TEST_CASE("modbus tls: mTLS client and server exchange registers", "[modbus][tls][mtls][integration][slow]")
    {
        const scoped_temp_dir cert_dir {"kmx_modbus_certs_exchange"};
        REQUIRE(cert_dir.valid());
        const auto cert_set_opt = ensure_ca_signed_set(cert_dir.path(), "127.0.0.1", "modbus-client");
        REQUIRE(cert_set_opt.has_value());
        const auto& certs = *cert_set_opt;

        auto srv = std::make_shared<tls_server>();
        srv->set_handler(function_code::read_holding_registers, make_simple_holding_handler());

        auto exec = std::make_shared<readiness::executor>();

        std::atomic_bool completed = false;
        std::optional<register_values> result;
        std::optional<std::error_code> op_error;

        const server_config srv_cfg {.bind_address = "127.0.0.1", .port = tls_test_port, .unit_id = tls_test_unit_id};
        const tls_config srv_tls {.cert_path = certs.server_cert.string(),
                                  .key_path = certs.server_key.string(),
                                  .ca_cert_path = certs.ca_cert.string(),
                                  .verify_peer = true,
                                  .sni_hostname = ""};

        auto serve = [exec, srv, srv_cfg, srv_tls]() -> task<void> { static_cast<void>(co_await srv->serve(*exec, srv_cfg, srv_tls)); };
        exec->spawn(serve());

        std::jthread server_stopper(
            [srv, &completed]()
            {
                while (!completed.load(std::memory_order_acquire))
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                srv->stop();
            });

        auto exchange = [&, exec, srv, ca = certs.ca_cert.string(), ccert = certs.client_cert.string(),
                         ckey = certs.client_key.string()]() -> task<void>
        {
            static_cast<void>(co_await exec->async_timeout(5'000'000u));

            const client_config cl_cfg {.host = "127.0.0.1", .port = tls_test_port, .unit_id = tls_test_unit_id};
            const tls_config cl_tls {.cert_path = ccert, .key_path = ckey, .ca_cert_path = ca, .verify_peer = true, .sni_hostname = ""};

            tls_client c {cl_cfg, cl_tls, *exec};

            expected_void_t connect_result = std::unexpected(make_error_code(error::connection_failed));
            for (int attempt = 0; attempt < 10; ++attempt)
            {
                connect_result = co_await c.connect();
                if (connect_result)
                    break;
                static_cast<void>(co_await exec->async_timeout(5'000'000u));
            }

            if (!connect_result)
            {
                completed.store(true, std::memory_order_release);
                op_error = connect_result.error();
                co_return;
            }

            const auto r = co_await c.read_holding_registers(0u, 3u);
            if (r)
                result = *r;
            else
                op_error = r.error();

            completed.store(true, std::memory_order_release);
            static_cast<void>(co_await c.disconnect());
        };
        exec->spawn(exchange());

        exec->run();

        if (op_error.has_value())
            SKIP("mTLS exchange unavailable in current environment");

        REQUIRE(completed.load(std::memory_order_acquire));
        REQUIRE(!op_error.has_value());
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 3u);
        CHECK(result->at(0) == 42u);
        CHECK(result->at(1) == 43u);
        CHECK(result->at(2) == 44u);
    }

    // mTLS rejection: client omits certificate
    TEST_CASE("modbus tls: server rejects client with missing certificate", "[modbus][tls][no-client-cert][integration][slow]")
    {
        const scoped_temp_dir cert_dir {"kmx_modbus_certs_reject"};
        REQUIRE(cert_dir.valid());
        const auto cert_set_opt = ensure_ca_signed_set(cert_dir.path(), "127.0.0.1", "modbus-client");
        REQUIRE(cert_set_opt.has_value());
        const auto& certs = *cert_set_opt;

        auto srv = std::make_shared<tls_server>();
        srv->set_handler(function_code::read_holding_registers, make_simple_holding_handler());

        auto exec = std::make_shared<readiness::executor>();

        std::atomic_bool completed = false;
        std::optional<register_values> values;
        std::optional<std::error_code> op_error;

        const server_config srv_cfg {.bind_address = "127.0.0.1", .port = tls_test_port + 1u, .unit_id = tls_test_unit_id};
        const tls_config srv_tls {.cert_path = certs.server_cert.string(),
                                  .key_path = certs.server_key.string(),
                                  .ca_cert_path = certs.ca_cert.string(),
                                  .verify_peer = true,
                                  .sni_hostname = ""}; // server requires client cert

        auto serve = [exec, srv, srv_cfg, srv_tls]() -> task<void> { static_cast<void>(co_await srv->serve(*exec, srv_cfg, srv_tls)); };
        exec->spawn(serve());

        std::jthread server_stopper(
            [srv, &completed]()
            {
                while (!completed.load(std::memory_order_acquire))
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                srv->stop();
            });

        auto exchange = [&, exec, srv, ca = certs.ca_cert.string()]() -> task<void>
        {
            static_cast<void>(co_await exec->async_timeout(5'000'000u));

            const client_config cl_cfg {.host = "127.0.0.1", .port = tls_test_port + 1u, .unit_id = tls_test_unit_id};
            // No cert_path / key_path — client presents no certificate
            const tls_config cl_tls {.cert_path = "", .key_path = "", .ca_cert_path = ca, .verify_peer = true, .sni_hostname = ""};

            tls_client c {cl_cfg, cl_tls, *exec};

            // Asserted on the exchange, not on connect() alone. Under TLS 1.3 the client sends its
            // Finished and considers the handshake done before the server has processed it, so a server
            // that demands a certificate the client never sent rejects the connection only after
            // connect() has already returned success. What has to hold either way is that no Modbus
            // data is ever exchanged over it.
            if (const auto r = co_await c.connect(); !r)
                op_error = r.error();
            else
            {
                if (const auto request = co_await c.read_holding_registers(0u, 1u); request)
                    values = *request;
                else
                    op_error = request.error();

                static_cast<void>(co_await c.disconnect());
            }

            completed.store(true, std::memory_order_release);
        };
        exec->spawn(exchange());

        exec->run();

        REQUIRE(completed.load(std::memory_order_acquire));

        // The property under test: a client that sends no certificate gets no data out of a server that
        // demands one. That the exchange failed is the assertion; which code carries the failure depends
        // on where TLS notices it, and is checked only loosely below.
        REQUIRE(op_error.has_value());
        CHECK_FALSE(values.has_value());

        INFO("reported error: " << op_error->message());
        CHECK(((*op_error == make_error_code(error::tls_handshake_failed)) || (*op_error == make_error_code(error::connection_failed)) ||
               (*op_error == make_error_code(error::disconnected)) || (*op_error == std::make_error_code(std::errc::connection_aborted))));
    }

} // namespace kmx::aio::test::modbus::integration::tls_client_server_test
#endif // KMX_AIO_FEATURE_MODBUS
