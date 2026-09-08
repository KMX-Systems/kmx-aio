/// @file kmx/aio/knx/gateway.cpp
/// @brief The compiled body of the KNX gateway facade.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/gateway.hpp>

namespace kmx::aio::knx
{
    expected_void_t gateway::start() noexcept
    {
        const auto reset_result = server_.reset();
        if (!reset_result.has_value())
            return reset_result;
        return router_.start();
    }

    expected_void_t gateway::stop() noexcept
    {
        const auto server_result = server_.shutdown();
        const auto router_result = router_.stop();
        if (!server_result.has_value())
            return server_result;
        return router_result;
    }
}
