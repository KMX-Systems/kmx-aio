/// @file src/kmx/aio/knx/routing/detail/secure_state.cpp
/// @brief The compiled body of the state a KNX IP Secure routing client holds beyond a plain one.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/routing/detail/secure_state.hpp>
#ifndef PCH
    #include <chrono>
    #include <utility>
#endif

namespace kmx::aio::knx::routing::detail
{
    secure_state::secure_state(secure_configuration value, const secure::monotonic_ms_function clock,
                               secure::entropy_source& source) noexcept(false):
        configuration(std::move(value)),
        clock_ms(clock),
        entropy(source),
        timer(configuration.latency_tolerance_ms, configuration.serial_number, configuration.duplicate_cache_entries, source)
    {
    }

    std::uint64_t secure_state::now_ms() const noexcept
    {
        if (clock_ms != nullptr)
            return clock_ms();
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }
}
