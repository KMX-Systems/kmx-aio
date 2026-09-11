/// @file src/kmx/aio/someip/error_test.cpp
/// @brief Unit tests for the SOME/IP error category name and messages.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/someip/error.hpp>
#ifndef PCH
    #include <catch2/catch_test_macros.hpp>

    #include <string>
    #include <system_error>
#endif

namespace kmx::aio::test::someip::error_test
{
    using namespace kmx::aio::someip;

    TEST_CASE("someip error category exposes stable name", "[someip][error]")
    {
        const std::error_code ec = make_error_code(error::feature_disabled);
        CHECK(ec.category().name() == std::string("someip"));
    }

    TEST_CASE("someip error messages are human readable", "[someip][error]")
    {
        const std::error_code ec = make_error_code(error::timed_out);
        CHECK(ec.message() == std::string("SOME/IP operation timed out"));
    }
}
