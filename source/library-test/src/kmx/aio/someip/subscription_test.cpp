/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/someip/client.hpp>
#include <kmx/aio/someip/subscription.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <system_error>

namespace kmx::aio::someip
{
    namespace detail
    {
        template <typename Result>
        struct coroutine_result_state
        {
            std::optional<Result> result;
            bool completed = false;
        };

        [[nodiscard]] client_config make_test_client_config()
        {
            return client_config {
                .application_name = "kmx_someip_subscription_client",
                .config_file_path = "",
                .service_id = 0x1111u,
                .instance_id = 0x2222u,
            };
        }

        [[nodiscard]] subscription_config make_test_subscription_config()
        {
            return subscription_config {
                .service_id = 0x1111u,
                .instance_id = 0x2222u,
                .event_group_id = 0x1000u,
                .event_ids = {0x1001u},
                .notification_queue_capacity = 16u,
            };
        }
    }

    using namespace detail;

    TEST_CASE("someip subscription open fails when not bound", "[someip][subscription]")
    {
        subscription s {make_test_subscription_config()};
        auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
        auto exec = std::make_shared<completion::executor>();

        exec->spawn(
            [&]() -> task<void>
            {
                state->result.emplace(co_await s.open());
                state->completed = true;
                exec->stop();
                co_return;
            }());
        exec->run();

        REQUIRE(state->completed);
        REQUIRE(state->result.has_value());
        REQUIRE_FALSE(state->result->has_value());
        CHECK(state->result->error() == make_error_code(error::invalid_configuration));
    }

    TEST_CASE("someip subscription next times out when queue is empty", "[someip][subscription]")
    {
        client c {make_test_client_config()};
        subscription s {c, make_test_subscription_config()};

        {
            auto exec = std::make_shared<completion::executor>();
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            exec->spawn(
                [&]() -> task<void>
                {
                    state->result.emplace(co_await c.start());
                    state->completed = true;
                    exec->stop();
                    co_return;
                }());
            exec->run();
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
        }

        {
            auto exec = std::make_shared<completion::executor>();
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            exec->spawn(
                [&]() -> task<void>
                {
                    state->result.emplace(co_await s.open());
                    state->completed = true;
                    exec->stop();
                    co_return;
                }());
            exec->run();
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
        }

        {
            auto exec = std::make_shared<completion::executor>();
            auto state = std::make_shared<coroutine_result_state<std::expected<event_notification, std::error_code>>>();
            exec->spawn(
                [&]() -> task<void>
                {
                    state->result.emplace(co_await s.next());
                    state->completed = true;
                    exec->stop();
                    co_return;
                }());
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            REQUIRE_FALSE(state->result->has_value());
            CHECK(state->result->error() == make_error_code(error::timed_out));
        }
    }

#if defined(KMX_AIO_FEATURE_SOMEIP) && (!__has_include(<vsomeip/vsomeip.hpp>) && !__has_include(<vsomeip3/vsomeip.hpp>))
    TEST_CASE("someip subscription dropped_events increases when capacity is zero", "[someip][subscription][queue]")
    {
        client c {make_test_client_config()};

        subscription_config cfg = make_test_subscription_config();
        cfg.notification_queue_capacity = 0u;
        subscription s {c, cfg};

        {
            auto exec = std::make_shared<completion::executor>();
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            exec->spawn(
                [&]() -> task<void>
                {
                    state->result.emplace(co_await c.start());
                    state->completed = true;
                    exec->stop();
                    co_return;
                }());
            exec->run();
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
        }

        {
            auto exec = std::make_shared<completion::executor>();
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            exec->spawn(
                [&]() -> task<void>
                {
                    state->result.emplace(co_await s.open());
                    state->completed = true;
                    exec->stop();
                    co_return;
                }());
            exec->run();
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
        }

        s.__kmx_test_push_event(event_notification {
            .service_id = 0x1111u,
            .instance_id = 0x2222u,
            .event_id = 0x1001u,
            .payload = {7u},
            .source_timestamp = std::chrono::system_clock::now(),
        });

        CHECK(s.dropped_events() == 1u);
    }

    TEST_CASE("someip subscription drops oldest when capacity is one", "[someip][subscription][queue]")
    {
        client c {make_test_client_config()};

        subscription_config cfg = make_test_subscription_config();
        cfg.notification_queue_capacity = 1u;
        subscription s {c, cfg};

        {
            auto exec = std::make_shared<completion::executor>();
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            exec->spawn(
                [&]() -> task<void>
                {
                    state->result.emplace(co_await c.start());
                    state->completed = true;
                    exec->stop();
                    co_return;
                }());
            exec->run();
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
        }

        {
            auto exec = std::make_shared<completion::executor>();
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            exec->spawn(
                [&]() -> task<void>
                {
                    state->result.emplace(co_await s.open());
                    state->completed = true;
                    exec->stop();
                    co_return;
                }());
            exec->run();
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
        }

        s.__kmx_test_push_event(event_notification {
            .service_id = 0x1111u,
            .instance_id = 0x2222u,
            .event_id = 0x1001u,
            .payload = {1u},
            .source_timestamp = std::chrono::system_clock::now(),
        });

        s.__kmx_test_push_event(event_notification {
            .service_id = 0x1111u,
            .instance_id = 0x2222u,
            .event_id = 0x1002u,
            .payload = {2u},
            .source_timestamp = std::chrono::system_clock::now(),
        });

        {
            auto exec = std::make_shared<completion::executor>();
            auto state = std::make_shared<coroutine_result_state<std::expected<event_notification, std::error_code>>>();
            exec->spawn(
                [&]() -> task<void>
                {
                    state->result.emplace(co_await s.next());
                    state->completed = true;
                    exec->stop();
                    co_return;
                }());
            exec->run();

            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
            CHECK(state->result->value().event_id == 0x1002u);
            CHECK(state->result->value().payload == std::vector<std::uint8_t>({2u}));
        }

        CHECK(s.dropped_events() == 1u);
    }

    TEST_CASE("someip subscription dropped_events resets across reopen", "[someip][subscription][queue][lifecycle]")
    {
        client c {make_test_client_config()};

        subscription_config cfg = make_test_subscription_config();
        cfg.notification_queue_capacity = 0u;
        subscription s {c, cfg};

        {
            auto exec = std::make_shared<completion::executor>();
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            exec->spawn(
                [&]() -> task<void>
                {
                    state->result.emplace(co_await c.start());
                    state->completed = true;
                    exec->stop();
                    co_return;
                }());
            exec->run();
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
        }

        {
            auto exec = std::make_shared<completion::executor>();
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            exec->spawn(
                [&]() -> task<void>
                {
                    state->result.emplace(co_await s.open());
                    state->completed = true;
                    exec->stop();
                    co_return;
                }());
            exec->run();
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
        }

        s.__kmx_test_push_event(event_notification {
            .service_id = 0x1111u,
            .instance_id = 0x2222u,
            .event_id = 0x1001u,
            .payload = {1u},
            .source_timestamp = std::chrono::system_clock::now(),
        });
        CHECK(s.dropped_events() == 1u);

        {
            auto exec = std::make_shared<completion::executor>();
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            exec->spawn(
                [&]() -> task<void>
                {
                    state->result.emplace(co_await s.close());
                    state->completed = true;
                    exec->stop();
                    co_return;
                }());
            exec->run();
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
        }

        {
            auto exec = std::make_shared<completion::executor>();
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            exec->spawn(
                [&]() -> task<void>
                {
                    state->result.emplace(co_await s.open());
                    state->completed = true;
                    exec->stop();
                    co_return;
                }());
            exec->run();
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
        }

        s.__kmx_test_push_event(event_notification {
            .service_id = 0x1111u,
            .instance_id = 0x2222u,
            .event_id = 0x1002u,
            .payload = {2u},
            .source_timestamp = std::chrono::system_clock::now(),
        });

        CHECK(s.dropped_events() == 1u);
    }
#endif
}
