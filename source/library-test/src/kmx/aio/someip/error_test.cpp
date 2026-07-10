/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/someip/error.hpp>

#include <string>
#include <system_error>

namespace kmx::aio::someip
{
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
