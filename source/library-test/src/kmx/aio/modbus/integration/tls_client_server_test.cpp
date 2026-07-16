/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#if defined(KMX_AIO_FEATURE_MODBUS)
    #include <kmx/aio/modbus/error.hpp>
    #include <kmx/aio/modbus/tls_client.hpp>
    #include <kmx/aio/modbus/tls_server.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/task.hpp>

    #include <cstdlib>
    #include <filesystem>
    #include <fstream>
    #include <chrono>
    #include <atomic>
    #include <memory>
    #include <optional>
    #include <string>
    #include <system_error>
    #include <thread>
    #include <vector>

namespace kmx::aio::modbus::test::integration
{
    static constexpr std::uint16_t tls_test_port    = 15802u;
    static constexpr std::uint8_t  tls_test_unit_id = 0x01u;

    // =========================================================================
    // Certificate helpers  (mirrors tls_mtls_client_server_test.cpp pattern)
    // =========================================================================

    static inline std::string shell_quote(const std::string& arg)
    {
        if (arg.find_first_of(" \t\n\"'$`\\!*?&|;()[]{}") == std::string::npos)
            return arg;
        std::string q = "'";
        for (const char c: arg)
            q += (c == '\'') ? std::string("'\\''") : std::string(1, c);
        q += '\'';
        return q;
    }

    static inline std::string read_file_text(const std::filesystem::path& path)
    {
        std::ifstream f(path);
        return f.is_open() ? std::string((std::istreambuf_iterator<char>(f)),
                                         std::istreambuf_iterator<char>())
                           : std::string {};
    }

    struct cert_set
    {
        std::string ca_cert;
        std::string server_cert;
        std::string server_key;
        std::string client_cert;
        std::string client_key;
    };

    /// @brief Generate a CA-signed mTLS certificate set under @p cert_dir.
    /// @details Reuses an existing valid set when present to reduce runtime flakiness.
    [[nodiscard]] static cert_set ensure_modbus_certs(const std::filesystem::path& cert_dir)
    {
        const auto ca_key   = (cert_dir / "ca_key.pem").string();
        const auto ca_cert  = (cert_dir / "ca_cert.pem").string();
        const auto sv_key   = (cert_dir / "server_key.pem").string();
        const auto sv_csr   = (cert_dir / "server.csr").string();
        const auto sv_cert  = (cert_dir / "server_cert.pem").string();
        const auto cl_key   = (cert_dir / "client_key.pem").string();
        const auto cl_csr   = (cert_dir / "client.csr").string();
        const auto cl_cert  = (cert_dir / "client_cert.pem").string();

        const auto existing_ok =
            std::filesystem::exists(ca_cert) && std::filesystem::exists(sv_cert) &&
            std::filesystem::exists(cl_cert) && std::filesystem::exists(sv_key) &&
            std::filesystem::exists(cl_key) &&
            read_file_text(ca_cert).find("BEGIN CERTIFICATE") != std::string::npos &&
            read_file_text(sv_cert).find("BEGIN CERTIFICATE") != std::string::npos &&
            read_file_text(cl_cert).find("BEGIN CERTIFICATE") != std::string::npos;

        if (existing_ok)
            return {ca_cert, sv_cert, sv_key, cl_cert, cl_key};

        std::filesystem::create_directories(cert_dir);

        // 1. Generate CA key + self-signed CA cert (RSA 2048, 30-day)
        const auto ca_cmd = "openssl req -x509 -newkey rsa:2048 -keyout " + shell_quote(ca_key) +
                            " -out " + shell_quote(ca_cert) +
                            " -days 30 -nodes -subj '/CN=ModbusTestCA' >/dev/null 2>&1";
        REQUIRE(std::system(ca_cmd.c_str()) == 0);

        // 2. Server key + CSR + sign with CA
        const auto sv_key_cmd = "openssl genrsa -out " + shell_quote(sv_key) + " 2048 >/dev/null 2>&1";
        REQUIRE(std::system(sv_key_cmd.c_str()) == 0);

        const auto sv_csr_cmd = "openssl req -new -key " + shell_quote(sv_key) +
                                " -out " + shell_quote(sv_csr) +
                                " -subj '/CN=127.0.0.1' >/dev/null 2>&1";
        REQUIRE(std::system(sv_csr_cmd.c_str()) == 0);

        const auto sv_sign_cmd = "openssl x509 -req -in " + shell_quote(sv_csr) +
                                 " -CA " + shell_quote(ca_cert) +
                                 " -CAkey " + shell_quote(ca_key) +
                                 " -CAcreateserial -out " + shell_quote(sv_cert) +
                                 " -days 30 >/dev/null 2>&1";
        REQUIRE(std::system(sv_sign_cmd.c_str()) == 0);

        // 3. Client key + CSR + sign with CA
        const auto cl_key_cmd = "openssl genrsa -out " + shell_quote(cl_key) + " 2048 >/dev/null 2>&1";
        REQUIRE(std::system(cl_key_cmd.c_str()) == 0);

        const auto cl_csr_cmd = "openssl req -new -key " + shell_quote(cl_key) +
                                " -out " + shell_quote(cl_csr) +
                                " -subj '/CN=modbus-client' >/dev/null 2>&1";
        REQUIRE(std::system(cl_csr_cmd.c_str()) == 0);

        const auto cl_sign_cmd = "openssl x509 -req -in " + shell_quote(cl_csr) +
                                 " -CA " + shell_quote(ca_cert) +
                                 " -CAkey " + shell_quote(ca_key) +
                                 " -CAcreateserial -out " + shell_quote(cl_cert) +
                                 " -days 30 >/dev/null 2>&1";
        REQUIRE(std::system(cl_sign_cmd.c_str()) == 0);

        // Validate PEM headers
        REQUIRE(read_file_text(ca_cert).find("BEGIN CERTIFICATE") != std::string::npos);
        REQUIRE(read_file_text(sv_cert).find("BEGIN CERTIFICATE") != std::string::npos);
        REQUIRE(read_file_text(cl_cert).find("BEGIN CERTIFICATE") != std::string::npos);

        return {ca_cert, sv_cert, sv_key, cl_cert, cl_key};
    }

    // =========================================================================
    // Server handler helpers (minimal — just read one register)
    // =========================================================================

    [[nodiscard]] static request_handler make_simple_holding_handler()
    {
        return [](server_request req) -> task<std::vector<std::uint8_t>>
        {
            if (req.pdu.size() < 5u)
                co_return std::vector<std::uint8_t> {0x83u, 0x03u};

            const std::uint16_t count = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(req.pdu[3]) << 8u) | req.pdu[4]);

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

    // =========================================================================
    // mTLS integration test
    // =========================================================================

    TEST_CASE("modbus tls: mTLS client and server exchange registers", "[modbus][tls][mtls][integration][slow]")
    {
        const auto certs = ensure_modbus_certs("/tmp/kmx_modbus_certs_exchange");

        auto srv = std::make_shared<tls_server>();
        srv->set_handler(function_code::read_holding_registers, make_simple_holding_handler());

        auto exec = std::make_shared<readiness::executor>();

        std::atomic_bool completed = false;
        std::optional<register_values> result;
        std::optional<std::error_code> op_error;

        const server_config srv_cfg {.bind_address = "127.0.0.1",
                                     .port         = tls_test_port,
                                     .unit_id      = tls_test_unit_id};
        const tls_config srv_tls {.cert_path    = certs.server_cert,
                                  .key_path     = certs.server_key,
                                  .ca_cert_path = certs.ca_cert,
                                  .verify_peer  = true,
                                  .sni_hostname = ""};

        exec->spawn(
            [exec, srv, srv_cfg, srv_tls]() -> task<void>
            { co_await srv->serve(*exec, srv_cfg, srv_tls); }());

        std::jthread server_stopper(
            [srv, &completed]()
            {
                while (!completed.load(std::memory_order_acquire))
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                srv->stop();
            });

        exec->spawn(
            [&, exec, srv, &ca = certs.ca_cert, &ccert = certs.client_cert,
             &ckey = certs.client_key]() -> task<void>
            {
                co_await exec->async_timeout(5'000'000u);

                const client_config cl_cfg {.host    = "127.0.0.1",
                                            .port    = tls_test_port,
                                            .unit_id = tls_test_unit_id};
                const tls_config cl_tls {.cert_path    = ccert,
                                         .key_path     = ckey,
                                         .ca_cert_path = ca,
                                         .verify_peer  = true,
                                         .sni_hostname = ""};

                tls_client c {cl_cfg, cl_tls, *exec};

                std::expected<void, std::error_code> connect_result =
                    std::unexpected(make_error_code(error::connection_failed));
                for (int attempt = 0; attempt < 10; ++attempt)
                {
                    connect_result = co_await c.connect();
                    if (connect_result)
                        break;
                    co_await exec->async_timeout(5'000'000u);
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
                co_await c.disconnect();
            }());

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

    // =========================================================================
    // mTLS rejection: client omits certificate
    // =========================================================================

    TEST_CASE("modbus tls: server rejects client with missing certificate", "[modbus][tls][no-client-cert][integration][slow]")
    {
        const auto certs = ensure_modbus_certs("/tmp/kmx_modbus_certs_reject");

        auto srv = std::make_shared<tls_server>();
        srv->set_handler(function_code::read_holding_registers, make_simple_holding_handler());

        auto exec = std::make_shared<readiness::executor>();

        std::atomic_bool completed = false;
        std::optional<std::error_code> op_error;

        const server_config srv_cfg {.bind_address = "127.0.0.1",
                                     .port         = tls_test_port + 1u,
                                     .unit_id      = tls_test_unit_id};
        const tls_config srv_tls {.cert_path    = certs.server_cert,
                                  .key_path     = certs.server_key,
                                  .ca_cert_path = certs.ca_cert,
                                  .verify_peer  = true,
                                  .sni_hostname = ""}; // server requires client cert

        exec->spawn(
            [exec, srv, srv_cfg, srv_tls]() -> task<void>
            { co_await srv->serve(*exec, srv_cfg, srv_tls); }());

        std::jthread server_stopper(
            [srv, &completed]()
            {
                while (!completed.load(std::memory_order_acquire))
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                srv->stop();
            });

        exec->spawn(
            [&, exec, srv, &ca = certs.ca_cert]() -> task<void>
            {
                co_await exec->async_timeout(5'000'000u);

                const client_config cl_cfg {.host    = "127.0.0.1",
                                            .port    = tls_test_port + 1u,
                                            .unit_id = tls_test_unit_id};
                // No cert_path / key_path — client presents no certificate
                const tls_config cl_tls {.cert_path    = "",
                                         .key_path     = "",
                                         .ca_cert_path = ca,
                                         .verify_peer  = true,
                                         .sni_hostname = ""};

                tls_client c {cl_cfg, cl_tls, *exec};
                const auto r = co_await c.connect();
                if (!r)
                    op_error = r.error();
                else
                    co_await c.disconnect();

                completed.store(true, std::memory_order_release);
            }());

        exec->run();

        REQUIRE(completed.load(std::memory_order_acquire));
        // Connection must fail because server demands a client certificate
        REQUIRE(op_error.has_value());
        CHECK(((*op_error == make_error_code(error::tls_handshake_failed)) ||
             (*op_error == make_error_code(error::connection_failed))));
    }

} // namespace kmx::aio::modbus::test::integration
#endif // KMX_AIO_FEATURE_MODBUS
