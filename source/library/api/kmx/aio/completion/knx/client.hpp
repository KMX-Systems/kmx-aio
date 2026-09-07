/// @file aio/completion/knx/client.hpp
/// @brief Completion-facing KNX tunnelling client alias.
#pragma once

#include <kmx/aio/knx/client.hpp>

namespace kmx::aio::completion::knx
{
    using client = kmx::aio::knx::tunnelling_client;
}
