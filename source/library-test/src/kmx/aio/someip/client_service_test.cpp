/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/someip/client.hpp>
#include <kmx/aio/someip/error.hpp>
#include <kmx/aio/test/executor_runner.hpp>
#include <kmx/aio/test/outcome.hpp>

#include <memory>
#include <optional>
#include <system_error>
#include <vector>

namespace kmx::aio::test::someip::client_service_test
{
    using namespace kmx::aio::someip;

    namespace detail
    {
        [[nodiscard]] client_config make_test_config()
        {
            return client_config {
                .application_name = "kmx_someip_test_client",
                .config_file_path = "",
                .service_id = 0x1111u,
                .instance_id = 0x2222u,
            };
        }

    }

    TEST_CASE("someip client start and stop succeed", "[someip][client][service]")
    {
        client c {detail::make_test_config()};

        {
            completion::executor exec;
            const auto state = run_awaited(exec, c.start());
            REQUIRE(state.has_value());
            REQUIRE(state->has_value());
        }

        const auto& stats = c.get_stats();
        CHECK(stats.start_attempts == 1u);
        CHECK(stats.successful_starts == 1u);
        CHECK(stats.dropped_events == 0u);

        {
            completion::executor exec;
            const auto state = run_awaited(exec, c.stop());
            REQUIRE(state.has_value());
            REQUIRE(state->has_value());
        }
    }

    TEST_CASE("someip client call fails when service unavailable", "[someip][client][service]")
    {
        client c {detail::make_test_config()};

        {
            completion::executor exec;
            const auto state = run_awaited(exec, c.start());
            REQUIRE(state.has_value());
            REQUIRE(state->has_value());
        }

        {
            completion::executor exec;
            const auto state = run_awaited(exec, c.call_method(0x1111u, 0x2222u, 0x3333u, {1u, 2u, 3u}));
            REQUIRE(state.has_value());
            REQUIRE_FALSE(state->has_value());
            CHECK(state->error() == make_error_code(error::service_unavailable));
        }
    }

    TEST_CASE("someip client call returns payload when service requested", "[someip][client][service]")
    {
        client c {detail::make_test_config()};

        {
            completion::executor exec;
            const auto state = run_awaited(exec, c.start());
            REQUIRE(state.has_value());
            REQUIRE(state->has_value());
        }

        {
            completion::executor exec;
            const auto request_state = run_awaited(exec, c.request_service(0x1111u, 0x2222u));
            REQUIRE(request_state.has_value());
            REQUIRE(request_state->has_value());
        }

        {
            completion::executor exec;
            const auto call_state = run_awaited(exec, c.call_method(0x1111u, 0x2222u, 0x3333u, {1u, 2u, 3u}));
            REQUIRE(call_state.has_value());
            REQUIRE(call_state->has_value());
            CHECK(call_state->value().payload == std::vector<std::uint8_t>({1u, 2u, 3u}));

            const auto& stats = c.get_stats();
            CHECK(stats.call_requests == 1u);
            CHECK(stats.calls_sent == 1u);
            CHECK(stats.calls_received == 1u);
            CHECK(stats.dropped_events == 0u);
        }
    }
} // namespace kmx::aio::test::someip::client_service_test
