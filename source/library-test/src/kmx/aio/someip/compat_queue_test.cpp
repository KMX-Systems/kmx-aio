/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/someip/vsomeip_compat.hpp>

#include <chrono>

namespace kmx::aio::someip::compat
{
#if defined(KMX_AIO_FEATURE_SOMEIP) && (!__has_include(<vsomeip/vsomeip.hpp>) && !__has_include(<vsomeip3/vsomeip.hpp>))

    TEST_CASE("someip compat queue drops oldest on capacity overflow", "[someip][compat][queue]")
    {
        client_runtime runtime {"kmx_someip_compat_queue_test", ""};
        REQUIRE(runtime.start());

        REQUIRE(runtime.subscribe(0x1111u, 0x2222u, 0x1000u, {0x1001u}, 1u));

        runtime.__kmx_test_push_event(event_notification {
            .service_id = 0x1111u,
            .instance_id = 0x2222u,
            .event_id = 0x1001u,
            .payload = {1u},
            .source_timestamp = std::chrono::system_clock::now(),
        });

        runtime.__kmx_test_push_event(event_notification {
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
        client_runtime runtime {"kmx_someip_compat_zero_test", ""};
        REQUIRE(runtime.start());

        REQUIRE(runtime.subscribe(0x1111u, 0x2222u, 0x1000u, {0x1001u}, 0u));

        runtime.__kmx_test_push_event(event_notification {
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
} // namespace kmx::aio::someip::compat
