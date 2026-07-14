/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/someip/client.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

namespace kmx::aio::someip
{
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

        template <typename Result>
        struct coroutine_result_state
        {
            std::optional<Result> result;
            bool completed = false;
        };

        task<void> run_start(client& c, std::shared_ptr<coroutine_result_state<std::expected<void, std::error_code>>> state,
                             completion::executor& exec)
        {
            state->result.emplace(co_await c.start());
            state->completed = true;
            exec.stop();
            co_return;
        }

        task<void> run_stop(client& c, std::shared_ptr<coroutine_result_state<std::expected<void, std::error_code>>> state,
                            completion::executor& exec)
        {
            state->result.emplace(co_await c.stop());
            state->completed = true;
            exec.stop();
            co_return;
        }

        task<void> run_call(client& c, std::shared_ptr<coroutine_result_state<std::expected<call_result, std::error_code>>> state,
                            completion::executor& exec)
        {
            state->result.emplace(co_await c.call_method(0x1111u, 0x2222u, 0x3333u, {1u, 2u, 3u}));
            state->completed = true;
            exec.stop();
            co_return;
        }
    }

    using namespace detail;

    TEST_CASE("someip client start and stop succeed", "[someip][client][service]")
    {
        client c {make_test_config()};

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            completion::executor exec;
            exec.spawn(run_start(c, state, exec));
            exec.run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
        }

        const auto& stats = c.get_stats();
        CHECK(stats.start_attempts == 1u);
        CHECK(stats.successful_starts == 1u);
        CHECK(stats.dropped_events == 0u);

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            completion::executor exec;
            exec.spawn(run_stop(c, state, exec));
            exec.run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
        }
    }

    TEST_CASE("someip client call fails when service unavailable", "[someip][client][service]")
    {
        client c {make_test_config()};

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            completion::executor exec;
            exec.spawn(run_start(c, state, exec));
            exec.run();
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
        }

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<call_result, std::error_code>>>();
            completion::executor exec;
            exec.spawn(run_call(c, state, exec));
            exec.run();
            REQUIRE(state->result.has_value());
            REQUIRE_FALSE(state->result->has_value());
            CHECK(state->result->error() == make_error_code(error::service_unavailable));
        }
    }

    TEST_CASE("someip client call returns payload when service requested", "[someip][client][service]")
    {
        client c {make_test_config()};

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            completion::executor exec;
            exec.spawn(run_start(c, state, exec));
            exec.run();
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
        }

        {
            auto request_state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            completion::executor exec;
            exec.spawn(
                [&]() -> task<void>
                {
                    request_state->result.emplace(co_await c.request_service(0x1111u, 0x2222u));
                    request_state->completed = true;
                    exec.stop();
                    co_return;
                }());
            exec.run();
            REQUIRE(request_state->completed);
            REQUIRE(request_state->result.has_value());
            REQUIRE(request_state->result->has_value());
        }

        {
            auto call_state = std::make_shared<coroutine_result_state<std::expected<call_result, std::error_code>>>();
            completion::executor exec;
            exec.spawn(run_call(c, call_state, exec));
            exec.run();
            REQUIRE(call_state->result.has_value());
            REQUIRE(call_state->result->has_value());
            CHECK(call_state->result->value().payload == std::vector<std::uint8_t>({1u, 2u, 3u}));

            const auto& stats = c.get_stats();
            CHECK(stats.call_requests == 1u);
            CHECK(stats.calls_sent == 1u);
            CHECK(stats.calls_received == 1u);
            CHECK(stats.dropped_events == 0u);
        }
    }
}
