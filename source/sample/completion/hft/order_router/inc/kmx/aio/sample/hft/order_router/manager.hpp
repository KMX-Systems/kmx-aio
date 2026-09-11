/// @file inc/kmx/aio/sample/hft/order_router/manager.hpp
/// @brief Order type, run constants and entry function of the HFT order router sample.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif

namespace kmx::aio::sample::hft::order_router
{
    /// @brief Which way an order trades.
    enum class side : std::uint8_t
    {
        buy,  ///< Buys the quantity.
        sell, ///< Sells the quantity.
    };

    /// @brief One order travelling from the market-data producer to the strategy consumer.
    struct ticket
    {
        std::uint64_t id {};        ///< Sequence number assigned by the producer.
        side direction {side::buy}; ///< Which way the order trades.
        double price {};            ///< Limit price.
        std::uint32_t quantity {};  ///< Number of units.
    };

    // Shared state
    /// @brief Slots in the channel between the producer and the consumer.
    constexpr std::size_t channel_capacity = 4096u;
    /// @brief Orders the producer generates in one run.
    constexpr std::size_t total_orders = 100'000u;

    /// @brief Routes every order from a pinned producer thread to a pinned consumer thread and prints the results.
    /// @return 0 when every order was accounted for, 1 otherwise.
    [[nodiscard]] int run();
}
