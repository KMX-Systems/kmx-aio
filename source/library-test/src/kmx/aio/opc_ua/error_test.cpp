/// @file src/kmx/aio/opc_ua/error_test.cpp
/// @brief Unit tests for the OPC UA error category name and messages.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/opc_ua/error.hpp>
#ifndef PCH
    #include <catch2/catch_test_macros.hpp>

    #include <string>
    #include <system_error>
#endif

namespace kmx::aio::test::opc_ua::error_test
{
    using namespace kmx::aio::opc_ua;

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
