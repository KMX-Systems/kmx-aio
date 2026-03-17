/// @file aio/allocator.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/allocator.hpp"

namespace kmx::aio
{
    thread_local slab_allocator* t_current_allocator = nullptr;

    void set_thread_allocator(slab_allocator* alloc) noexcept
    {
        t_current_allocator = alloc;
    }

    slab_allocator* get_thread_allocator() noexcept
    {
        return t_current_allocator;
    }
} // namespace kmx::aio
