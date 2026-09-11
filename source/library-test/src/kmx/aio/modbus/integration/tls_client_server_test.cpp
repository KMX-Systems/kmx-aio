/// @file src/kmx/aio/modbus/integration/tls_client_server_test.cpp
/// @brief Modbus/TLS client and server over loopback mTLS: a register exchange, and refusal of a certificateless client.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#if defined(KMX_AIO_FEATURE_MODBUS)
    #ifndef PCH
        #include <kmx/aio/modbus/error.hpp>
        #include <kmx/aio/modbus/tls_client.hpp>
        #include <kmx/aio/modbus/tls_server.hpp>
        #include <kmx/aio/readiness/executor.hpp>
        #include <kmx/aio/task.hpp>
        #include <kmx/aio/test/sample_process.hpp>
        #include <kmx/aio/test/scoped_temp_dir.hpp>
        #include <kmx/aio/test/tls_certs.hpp>

        #include <catch2/catch_test_macros.hpp>

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
    #endif

namespace kmx::aio::test::modbus::integration::tls_client_server_test
{
    using namespace kmx::aio::modbus;

    static constexpr std::uint16_t base_port = 15802u;
    static constexpr std::uint8_t unit_id = 0x01u;

    /// @brief What one client exchange observed, read by the test once the executor has drained.
    struct exchange_outcome
    {
        /// @brief Set once the exchange finished, successfully or not; the server is stopped when it is.
        std::atomic_bool completed {};
        /// @brief Why the exchange failed, if it did.
        std::optional<std::error_code> op_error {};
        /// @brief The registers read, if the read succeeded.
        std::optional<register_values> values {};
    };

    namespace detail
    {
        /// @brief Connects with a client certificate, reads three registers, and disconnects.
        /// @param exec The executor the client runs on.
        /// @param outcome Receives what the exchange observed.
        /// @param cl_tls The client's TLS settings: its certificate and key, and the CA it trusts. Taken by value, as the
        ///               coroutine outlives its caller's temporaries.
        task<void> exchange_over_mtls(const std::shared_ptr<readiness::executor>& exec, exchange_outcome& outcome,
                                      const tls_config cl_tls) noexcept(false)
        {
            static_cast<void>(co_await exec->async_timeout(5'000'000u));

            const client_config cl_cfg {.host = "127.0.0.1", .port = base_port, .unit_id = unit_id};

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
                outcome.completed.store(true, std::memory_order_release);
                outcome.op_error = connect_result.error();
                co_return;
            }

            const auto r = co_await c.read_holding_registers(0u, 3u);
            if (r)
                outcome.values = *r;
            else
                outcome.op_error = r.error();

            outcome.completed.store(true, std::memory_order_release);
            static_cast<void>(co_await c.disconnect());
        }

        /// @brief Connects presenting no certificate, and records that no data was exchanged.
        /// @param exec The executor the client runs on.
        /// @param outcome Receives what the exchange observed.
        /// @param ca_cert_path The CA the client trusts; taken by value, as the coroutine outlives its caller's temporaries.
        task<void> exchange_without_certificate(const std::shared_ptr<readiness::executor>& exec, exchange_outcome& outcome,
                                                const std::string ca_cert_path) noexcept(false)
        {
            static_cast<void>(co_await exec->async_timeout(5'000'000u));

            const client_config cl_cfg {.host = "127.0.0.1", .port = base_port + 1u, .unit_id = unit_id};
            // No cert_path / key_path — client presents no certificate
            const tls_config cl_tls {
                .cert_path = "", .key_path = "", .ca_cert_path = ca_cert_path, .verify_peer = true, .sni_hostname = ""};

            tls_client c {cl_cfg, cl_tls, *exec};

            // Asserted on the exchange, not on connect() alone. Under TLS 1.3 the client sends its
            // Finished and considers the handshake done before the server has processed it, so a server
            // that demands a certificate the client never sent rejects the connection only after
            // connect() has already returned success. What has to hold either way is that no Modbus
            // data is ever exchanged over it.
            if (const auto r = co_await c.connect(); !r)
                outcome.op_error = r.error();
            else
            {
                if (const auto request = co_await c.read_holding_registers(0u, 1u); request)
                    outcome.values = *request;
                else
                    outcome.op_error = request.error();

                static_cast<void>(co_await c.disconnect());
            }

            outcome.completed.store(true, std::memory_order_release);
        }
    }

    namespace detail
    {
        /// @brief Answers one read-holding-registers request with deterministic values.
        /// @param req The request PDU as the server framed it.
        /// @return The response PDU.
        /// @throws std::bad_alloc (coroutine frame and response allocation).
        [[nodiscard]] task<std::vector<std::uint8_t>> simple_holding_response(server_request req) noexcept(false)
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
        }
    }

    // Server handler helpers (minimal — just read one register)
    [[nodiscard]] static request_handler make_simple_holding_handler()
    {
        return [](server_request req) { return detail::simple_holding_response(std::move(req)); };
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

        exchange_outcome outcome {};

        const server_config srv_cfg {.bind_address = "127.0.0.1", .port = base_port, .unit_id = unit_id};
        const tls_config srv_tls {.cert_path = certs.server_cert.string(),
                                  .key_path = certs.server_key.string(),
                                  .ca_cert_path = certs.ca_cert.string(),
                                  .verify_peer = true,
                                  .sni_hostname = ""};

        auto serve = [exec, srv, srv_cfg, srv_tls]() -> task<void> { static_cast<void>(co_await srv->serve(*exec, srv_cfg, srv_tls)); };
        exec->spawn(serve());

        std::jthread server_stopper(
            [srv, &outcome]()
            {
                while (!outcome.completed.load(std::memory_order_acquire))
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                srv->stop();
            });

        exec->spawn(detail::exchange_over_mtls(exec, outcome,
                                               {.cert_path = certs.client_cert.string(),
                                                .key_path = certs.client_key.string(),
                                                .ca_cert_path = certs.ca_cert.string(),
                                                .verify_peer = true,
                                                .sni_hostname = ""}));

        exec->run();

        if (outcome.op_error.has_value())
            SKIP("mTLS exchange unavailable in current environment");

        REQUIRE(outcome.completed.load(std::memory_order_acquire));
        REQUIRE(!outcome.op_error.has_value());
        REQUIRE(outcome.values.has_value());
        REQUIRE(outcome.values->size() == 3u);
        CHECK(outcome.values->at(0) == 42u);
        CHECK(outcome.values->at(1) == 43u);
        CHECK(outcome.values->at(2) == 44u);
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

        exchange_outcome outcome {};

        const server_config srv_cfg {.bind_address = "127.0.0.1", .port = base_port + 1u, .unit_id = unit_id};
        const tls_config srv_tls {.cert_path = certs.server_cert.string(),
                                  .key_path = certs.server_key.string(),
                                  .ca_cert_path = certs.ca_cert.string(),
                                  .verify_peer = true,
                                  .sni_hostname = ""}; // server requires client cert

        auto serve = [exec, srv, srv_cfg, srv_tls]() -> task<void> { static_cast<void>(co_await srv->serve(*exec, srv_cfg, srv_tls)); };
        exec->spawn(serve());

        std::jthread server_stopper(
            [srv, &outcome]()
            {
                while (!outcome.completed.load(std::memory_order_acquire))
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                srv->stop();
            });

        exec->spawn(detail::exchange_without_certificate(exec, outcome, certs.ca_cert.string()));

        exec->run();

        REQUIRE(outcome.completed.load(std::memory_order_acquire));

        // The property under test: a client that sends no certificate gets no data out of a server that
        // demands one. That the exchange failed is the assertion; which code carries the failure depends
        // on where TLS notices it, and is checked only loosely below.
        REQUIRE(outcome.op_error.has_value());
        CHECK_FALSE(outcome.values.has_value());

        // Under TLS 1.3 the refusal usually arrives as the server's alert on the first read after the client's
        // handshake has completed, which tls::basic_stream reports as std::errc::protocol_error.
        const std::error_code& op_error = *outcome.op_error;
        INFO("reported error: " << op_error.message());
        CHECK(((op_error == make_error_code(error::tls_handshake_failed)) || (op_error == make_error_code(error::connection_failed)) ||
               (op_error == make_error_code(error::disconnected)) || (op_error == std::make_error_code(std::errc::connection_aborted)) ||
               (op_error == std::make_error_code(std::errc::protocol_error))));
    }

}
#endif // KMX_AIO_FEATURE_MODBUS
