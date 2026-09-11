/// @file src/kmx/aio/knx/keyring/document.cpp
/// @brief Lookups in a loaded ETS keyring: interfaces and devices by individual address, group keys by group address.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/keyring/document.hpp>
#ifndef PCH
    #include <algorithm>
#endif

namespace kmx::aio::knx::keyring
{
    const interface_entry* document::find_interface(const individual_address address) const noexcept
    {
        const auto found = std::ranges::find(interfaces, address, &interface_entry::address);
        return (found == interfaces.end()) ? nullptr : &*found;
    }

    const device* document::find_device(const individual_address address) const noexcept
    {
        const auto found = std::ranges::find(devices, address, &device::address);
        return (found == devices.end()) ? nullptr : &*found;
    }

    const group_key* document::find_group_key(const group_address address) const noexcept
    {
        const auto found = std::ranges::find(group_keys, address, &group_key::address);
        return (found == group_keys.end()) ? nullptr : &*found;
    }
}
