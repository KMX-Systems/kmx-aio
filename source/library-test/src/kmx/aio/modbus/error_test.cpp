/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/modbus/error.hpp>

#include <string>
#include <system_error>

namespace kmx::aio::modbus
{
    TEST_CASE("modbus error category exposes stable name", "[modbus][error][unit]")
    {
        const std::error_code ec = make_error_code(error::connection_failed);
        CHECK(ec.category().name() == std::string("modbus"));
    }

    TEST_CASE("modbus error codes have human readable messages", "[modbus][error][unit]")
    {
        CHECK(make_error_code(error::success).message() == std::string("success"));
        CHECK(make_error_code(error::feature_disabled).message() == std::string("Modbus feature is disabled"));
        CHECK(make_error_code(error::invalid_configuration).message() == std::string("Modbus configuration is invalid"));
        CHECK(make_error_code(error::connection_failed).message() == std::string("Modbus connection failed"));
        CHECK(make_error_code(error::disconnected).message() == std::string("Modbus peer is disconnected"));
        CHECK(make_error_code(error::exception_response).message() == std::string("Modbus server returned an exception response"));
        CHECK(make_error_code(error::unexpected_function_code).message() == std::string("Modbus response function code does not match request"));
        CHECK(make_error_code(error::unexpected_transaction_id).message() == std::string("Modbus response transaction identifier does not match request"));
        CHECK(make_error_code(error::frame_too_large).message() == std::string("Modbus request PDU exceeds the 253-byte protocol limit"));
        CHECK(make_error_code(error::malformed_frame).message() == std::string("Modbus frame is malformed or truncated"));
        CHECK(make_error_code(error::invalid_unit_id).message() == std::string("Modbus response unit identifier does not match request"));
        CHECK(make_error_code(error::tls_handshake_failed).message() == std::string("Modbus TLS handshake failed"));
        CHECK(make_error_code(error::timed_out).message() == std::string("Modbus operation timed out"));
        CHECK(make_error_code(error::internal_error).message() == std::string("Modbus internal error"));
    }

    TEST_CASE("modbus error is_error_code_enum enables implicit conversion", "[modbus][error][unit]")
    {
        const std::error_code ec = error::timed_out;
        CHECK(ec.category().name() == std::string("modbus"));
        CHECK(ec.value() == static_cast<int>(error::timed_out));
    }

    TEST_CASE("modbus error category singleton is stable across calls", "[modbus][error][unit]")
    {
        CHECK(&error_category() == &error_category());
        CHECK(&make_error_code(error::disconnected).category() == &error_category());
    }

} // namespace kmx::aio::modbus
