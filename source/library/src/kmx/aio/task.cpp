/// @file aio/task.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/task.hpp"
#include "kmx/aio/allocator.hpp"
#include <new>

namespace kmx::aio::detail
{
    void* promise_base::operator new(std::size_t size) noexcept(false)
    {
        if (auto* alloc = get_thread_allocator())
        {
            if (size <= alloc->slot_size())
            {
                if (void* ptr = alloc->allocate())
                {
                    return ptr;
                }
            }
        }
        return ::operator new(size);
    }

    void promise_base::operator delete(void* ptr, std::size_t) noexcept
    {
        if (auto* alloc = get_thread_allocator())
        {
            if (alloc->owns(ptr))
            {
                alloc->deallocate(ptr);
                return;
            }
        }
        ::operator delete(ptr);
    }
} // namespace kmx::aio::detail
