/// @file inc/kmx/aio/detail/scoped_fault.hpp
/// @brief A scope guard that arms a system-call fault and disarms it however the scope ends.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#if defined(KMX_AIO_FAULT_INJECTION)
    #ifndef PCH
        #include <kmx/aio/detail/fault_registry.hpp>
    #endif

namespace kmx::aio::detail
{
    /// @brief Arms a fault for the duration of a scope and disarms it however the scope ends.
    class scoped_fault
    {
    public:
        /// @param id    The call to fail.
        /// @param error The errno the call should report.
        /// @param times How many calls to fail before letting them through again.
        /// @param skip  How many calls to let through before the first failure.
        scoped_fault(const syscall_id id, const int error, const unsigned times = 1u, const unsigned skip = 0u) noexcept: id_(id)
        {
            fault_registry::arm(id, error, times, skip);
        }

        scoped_fault(const scoped_fault&) = delete;
        scoped_fault& operator=(const scoped_fault&) = delete;

        ~scoped_fault() noexcept { fault_registry::disarm(id_); }

    private:
        syscall_id id_;
    };
}
#endif // KMX_AIO_FAULT_INJECTION
