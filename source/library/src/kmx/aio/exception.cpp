/// @file src/kmx/aio/exception.cpp
/// @brief Out-of-line key function that anchors the vtable of the library exception base class.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/exception.hpp>

namespace kmx::aio
{
    // Out of line so the class has one key function, and every translation unit that catches a library
    // exception shares a single vtable and typeinfo for it rather than emitting a weak copy of its own.
    exception::~exception() noexcept = default;
}
