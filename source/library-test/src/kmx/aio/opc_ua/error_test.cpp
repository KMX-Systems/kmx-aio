/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/opc_ua/error.hpp>

#include <string>
#include <system_error>

namespace kmx::aio::opc_ua
{
    TEST_CASE("opc_ua error category exposes stable name", "[opc_ua][error]")
    {
        const std::error_code ec = make_error_code(error::feature_disabled);
        CHECK(ec.category().name() == std::string("opc_ua"));
    }

    TEST_CASE("opc_ua error messages are human readable", "[opc_ua][error]")
    {
        const std::error_code ec = make_error_code(error::timed_out);
        CHECK(ec.message() == std::string("OPC UA operation timed out"));
    }
}
