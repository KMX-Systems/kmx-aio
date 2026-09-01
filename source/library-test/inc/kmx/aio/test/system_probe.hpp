/// @file aio/test/system_probe.hpp
/// @brief Environment predicates that decide whether a test can run at all.
/// @details These gate SKIP()s rather than assertions. A test that pins a thread to a core, needs
///          hugepages, or opens a raw socket is testing the library, not the machine, so on a host that
///          cannot offer what it needs the honest outcome is "skipped", not "failed".
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <expected>
    #include <fstream>
    #include <string>
    #include <system_error>

    #include <linux/if_ether.h>
    #include <netinet/in.h>
    #include <pthread.h>
    #include <sched.h>
    #include <sys/socket.h>
    #include <unistd.h>

    #include <kmx/aio/basic_types.hpp>
#endif

namespace kmx::aio::test
{
    /// @brief The first CPU this thread is allowed to run on.
    /// @details Pinning to a core outside the process's own affinity mask fails, and on a machine under
    ///          cgroup or taskset restrictions core 0 need not be in it - so a test that assumes core 0
    ///          fails on exactly the machines that restrict it most.
    /// @return The lowest allowed CPU index, or the error that reading the mask reported.
    [[nodiscard]] inline expected_int_t first_allowed_cpu() noexcept
    {
        cpu_set_t allowed {};
        CPU_ZERO(&allowed);

        const int ret = ::pthread_getaffinity_np(::pthread_self(), sizeof(cpu_set_t), &allowed);
        if (ret != 0)
            return std::unexpected(std::error_code(ret, std::generic_category()));

        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
            if (CPU_ISSET(cpu, &allowed) != 0)
                return cpu;

        return std::unexpected(std::make_error_code(std::errc::no_such_device));
    }

    /// @brief Whether the host has hugepages reserved, which SPDK's DPDK layer needs to initialise.
    /// @return True when HugePages_Total is above zero.
    [[nodiscard]] inline bool hugepages_available() noexcept
    {
        std::ifstream meminfo {"/proc/meminfo"};
        if (!meminfo)
            return false;

        std::string label;
        std::uint64_t value {};
        std::string unit;
        while (meminfo >> label >> value >> unit)
            if (label == "HugePages_Total:")
                return value > 0u;

        return false;
    }

    /// @brief Whether the process may open raw packet sockets.
    /// @details Attempts a zero-payload AF_PACKET socket, which is the cheapest reliable check - parsing
    ///          /proc/self/status would have to account for both the effective set and for running as
    ///          root.
    /// @return True when the process holds CAP_NET_RAW, or is root.
    [[nodiscard]] inline bool has_cap_net_raw() noexcept
    {
        const int fd = ::socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, static_cast<int>(::htons(ETH_P_ALL)));
        if (fd < 0)
            return false;

        ::close(fd);
        return true;
    }

} // namespace kmx::aio::test
