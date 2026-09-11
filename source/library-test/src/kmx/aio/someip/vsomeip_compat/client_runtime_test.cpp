/// @file src/kmx/aio/someip/vsomeip_compat/client_runtime_test.cpp
/// @brief Unit tests for the drop-oldest event queue of the SOME/IP compat runtime when built without vsomeip.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/someip/vsomeip_compat/client_runtime.hpp>
#ifndef PCH
    #include <kmx/aio/someip/types.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <chrono>
    #include <cstdint>
    #include <vector>
#endif

namespace kmx::aio::test::someip::vsomeip_compat::client_runtime_test
{
#if defined(KMX_AIO_FEATURE_SOMEIP) && (!__has_include(<vsomeip/vsomeip.hpp>) && !__has_include(<vsomeip3/vsomeip.hpp>))

    TEST_CASE("someip compat queue drops oldest on capacity overflow", "[someip][compat][queue]")
    {
        kmx::aio::someip::vsomeip_compat::client_runtime runtime {"kmx_someip_compat_queue_test", ""};
        REQUIRE(runtime.start());

        REQUIRE(runtime.subscribe({
            .service_id = 0x1111u,
            .instance_id = 0x2222u,
            .event_group_id = 0x1000u,
            .event_ids = {0x1001u},
            .notification_queue_capacity = 1u,
        }));

        runtime.test_push_event(kmx::aio::someip::event_notification {
            .service_id = 0x1111u,
            .instance_id = 0x2222u,
            .event_id = 0x1001u,
            .payload = {1u},
            .source_timestamp = std::chrono::system_clock::now(),
        });

        runtime.test_push_event(kmx::aio::someip::event_notification {
            .service_id = 0x1111u,
            .instance_id = 0x2222u,
            .event_id = 0x1002u,
            .payload = {2u},
            .source_timestamp = std::chrono::system_clock::now(),
        });

        const auto event = runtime.next_event(std::chrono::milliseconds(0));
        REQUIRE(event.has_value());
        CHECK(event->event_id == 0x1002u);
        CHECK(event->payload == std::vector<std::uint8_t>({2u}));

        const auto none = runtime.next_event(std::chrono::milliseconds(0));
        CHECK_FALSE(none.has_value());

        CHECK(runtime.dropped_events() == 1u);

        CHECK(runtime.stop());
    }

    TEST_CASE("someip compat queue capacity zero drops all events", "[someip][compat][queue]")
    {
        kmx::aio::someip::vsomeip_compat::client_runtime runtime {"kmx_someip_compat_zero_test", ""};
        REQUIRE(runtime.start());

        REQUIRE(runtime.subscribe({
            .service_id = 0x1111u,
            .instance_id = 0x2222u,
            .event_group_id = 0x1000u,
            .event_ids = {0x1001u},
            .notification_queue_capacity = 0u,
        }));

        runtime.test_push_event(kmx::aio::someip::event_notification {
            .service_id = 0x1111u,
            .instance_id = 0x2222u,
            .event_id = 0x1001u,
            .payload = {9u},
            .source_timestamp = std::chrono::system_clock::now(),
        });

        const auto event = runtime.next_event(std::chrono::milliseconds(0));
        CHECK_FALSE(event.has_value());
        CHECK(runtime.dropped_events() == 1u);

        CHECK(runtime.stop());
    }

#endif
}
