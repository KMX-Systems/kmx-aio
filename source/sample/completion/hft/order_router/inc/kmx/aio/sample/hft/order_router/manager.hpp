#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include <kmx/aio/channel.hpp>

namespace kmx::aio::sample::hft::order_router
{
    enum class side : std::uint8_t
    {
        buy,
        sell,
    };

    struct order
    {
        std::uint64_t id {};
        side          direction {side::buy};
        double        price {};
        std::uint32_t quantity {};
    };

    // Shared state
    constexpr std::size_t channel_capacity  = 4096u;
    constexpr std::size_t total_orders      = 100'000u;

    auto execute_order_router() -> int;
}
