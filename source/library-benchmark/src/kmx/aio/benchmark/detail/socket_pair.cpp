/// @file src/kmx/aio/benchmark/detail/socket_pair.cpp
/// @brief Anonymous socket pair implementation: closing whichever ends were opened.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/benchmark/detail/socket_pair.hpp>
#ifndef PCH
    #include <unistd.h>
#endif

namespace kmx::aio::benchmark::detail
{
    socket_pair::~socket_pair() noexcept
    {
        for (const int f: fd)
            if (f >= 0)
                ::close(f);
    }
}
