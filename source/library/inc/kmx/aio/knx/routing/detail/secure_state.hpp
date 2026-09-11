/// @file inc/kmx/aio/knx/routing/detail/secure_state.hpp
/// @brief The state a KNX IP Secure routing client holds beyond a plain one.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/async_mutex.hpp>
        #include <kmx/aio/knx/frame.hpp>
        #include <kmx/aio/knx/routing.hpp>
        #include <kmx/aio/knx/secure/common.hpp>
        #include <kmx/aio/knx/secure/entropy_source.hpp>
        #include <kmx/aio/knx/secure/routing_timer_state.hpp>

        #include <array>
        #include <atomic>
        #include <cstdint>
        #include <limits>
    #endif

namespace kmx::aio::knx::routing::detail
{
    /// @brief What a secure routing client holds beyond a plain one.
    /// @details The receive, send and notify paths share everything here, and touch it only under @ref mutex,
    ///          which is never held across I/O. @ref next_deadline_ms is republished under the mutex so that it
    ///          can be read without taking it.
    struct secure_state
    {
        secure_state(secure_configuration value, secure::monotonic_ms_function clock, secure::entropy_source& source) noexcept(false);

        [[nodiscard]] std::uint64_t now_ms() const noexcept;

        /// @brief Publishes what readers without the mutex may see: when the notify path next has work - at once
        ///        while a synchronisation request is owed - and whether the timer has synchronised.
        void publish_state() noexcept
        {
            next_deadline_ms.store(synchronisation_due ? 0u : timer.next_deadline_ms(), std::memory_order_relaxed);
            synchronised.store(timer.synchronised(), std::memory_order_relaxed);
        }

        secure_configuration configuration;
        secure::monotonic_ms_function clock_ms;
        secure::entropy_source& entropy;
        secure::routing_timer_state timer;
        async_mutex mutex {};
        bool synchronisation_due {};
        std::atomic<std::uint64_t> next_deadline_ms {std::numeric_limits<std::uint64_t>::max()};
        std::atomic<bool> synchronised {};
        /// @brief Where received wrappers are decrypted into; on the heap, not on a coroutine frame.
        std::array<std::uint8_t, frame::max_datagram_size> plain {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
