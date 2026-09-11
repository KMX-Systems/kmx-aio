/// @file src/main.cpp
/// @brief Entry point of the HFT order router sample: runs order_router::run().
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#ifndef PCH
    #include <kmx/aio/sample/hft/order_router/manager.hpp>
#endif

// Entry point
int main() noexcept
{
    return kmx::aio::sample::hft::order_router::run();
}
