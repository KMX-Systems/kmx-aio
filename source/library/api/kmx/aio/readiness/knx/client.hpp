/// @file aio/readiness/knx/client.hpp
/// @brief Readiness-facing KNX tunnelling client alias.
#pragma once

#include <kmx/aio/knx/client.hpp>

namespace kmx::aio::readiness::knx
{
    using client = kmx::aio::knx::tunnelling_client;
}
